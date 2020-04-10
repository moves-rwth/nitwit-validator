import argparse
import json
import multiprocessing
import os
import random
import resource
import subprocess
import sys
from typing import List, Tuple

from common.utils import process_results
from common.utils import parse_message

WITNESSES_BY_PROGRAM_HASH_DIR = "witnessListByProgramHashJSON"
WITNESS_INFO_BY_WITNESS_HASH_DIR = "witnessInfoByHash"
WITNESS_FILE_BY_HASH_DIR = "witnessFileByHash"
SV_BENCHMARK_DIR = ""
VALIDATOR_EXECUTABLE = ""
EXECUTION_TIMEOUT = 0

#'output code', 'witness file', 'extracted output message', 'runtime (secs)', 'witness producer', 'peak memory (bytes)')
BENCH_RESULTS_HEADER = ('status', 'wit_key', 'out', 'cpu', 'tool', 'mem')

task_queue = multiprocessing.Queue()
result_queue = multiprocessing.Queue()


def setup_dirs(dir: str, sv_dir: str, executable: str, timeout: float) -> bool:
    if not os.path.exists(dir) or not os.path.isdir(dir):
        print(f"The directory {dir} doesn't exist or is not a directory.", file=sys.stderr)
        return False

    global WITNESS_INFO_BY_WITNESS_HASH_DIR, WITNESSES_BY_PROGRAM_HASH_DIR, WITNESS_FILE_BY_HASH_DIR, SV_BENCHMARK_DIR, \
        VALIDATOR_EXECUTABLE, EXECUTION_TIMEOUT
    WITNESS_INFO_BY_WITNESS_HASH_DIR = os.path.join(dir, WITNESS_INFO_BY_WITNESS_HASH_DIR)
    WITNESSES_BY_PROGRAM_HASH_DIR = os.path.join(dir, WITNESSES_BY_PROGRAM_HASH_DIR)
    WITNESS_FILE_BY_HASH_DIR = os.path.join(dir, WITNESS_FILE_BY_HASH_DIR)
    SV_BENCHMARK_DIR = os.path.join(sv_dir, 'c')
    VALIDATOR_EXECUTABLE = os.path.abspath(executable)
    EXECUTION_TIMEOUT = timeout

    if not (os.path.exists(WITNESS_INFO_BY_WITNESS_HASH_DIR) and os.path.isdir(WITNESS_INFO_BY_WITNESS_HASH_DIR) and
            os.path.exists(WITNESS_FILE_BY_HASH_DIR) and os.path.isdir(WITNESS_FILE_BY_HASH_DIR) and
            os.path.exists(WITNESSES_BY_PROGRAM_HASH_DIR) and os.path.isdir(WITNESSES_BY_PROGRAM_HASH_DIR)):
        print(f"The directory {dir} doesn't contain the necessary directories.", file=sys.stderr)
        return False
    if not (os.path.exists(SV_BENCHMARK_DIR) and os.path.isdir(SV_BENCHMARK_DIR)):
        print(f"The directory {sv_dir} doesn't exist or isn't a directory.", file=sys.stderr)
        return False
    if not (os.path.exists(VALIDATOR_EXECUTABLE) and os.path.isfile(VALIDATOR_EXECUTABLE)):
        print(f"The executable {executable} doesn't exist or isn't a file")
        return False
    print("Directories found!")
    return True


def run_validator():
    config = task_queue.get()
    if config is None:
        result_queue.put(None)
        print(f"Sending kill pill")
        return
    witness, source, info_file, producer = config
    with subprocess.Popen([VALIDATOR_EXECUTABLE, witness, source], shell=False,
                          stdout=subprocess.PIPE,
                          stderr=subprocess.DEVNULL) as process:
        errmsg = ''
        try:
            out, _ = process.communicate(timeout=EXECUTION_TIMEOUT)
            errmsg = parse_message(errmsg, out, process)
        except subprocess.TimeoutExpired:
            process.kill()
        finally:
            if process.poll() is None:
                process.kill()
                _, _ = process.communicate()

        returncode = process.returncode
        if errmsg == 'out of memory':  # hack around no special exit code for o/m
            returncode = 251
        children = resource.getrusage(resource.RUSAGE_CHILDREN)
        result_queue.put((returncode,
                          info_file,
                          errmsg,
                          children.ru_utime + children.ru_stime,  # - (children_before.ru_utime + children_before.ru_stime),
                          producer,
                          children.ru_maxrss))


def run_bench_parallel(configs: List[Tuple[str, str, str, str]], n_processes: int) -> List[Tuple[int, str, str, float, str, int]]:
    # random.shuffle(configs)
    # with multiprocessing.Pool(n_processes) as pool:
    #     results = pool.map(run_validator, configs)

    for i in range(n_processes):  # add kill pills, launch processes
        configs.append(None)
        multiprocessing.Process(target=run_validator, ).start()
    for config in configs:  # add all tasks
        task_queue.put(config)
    pills = 0
    results = []
    print(f"Processing {len(configs) - n_processes} tasks:")
    while True:
        res = result_queue.get()
        if res is None:
            pills += 1
            if pills == n_processes:
                break
            else:
                continue
        results.append(res)
        if len(results) % 500 == 0:
            print(f"...{len(results)}")
        multiprocessing.Process(target=run_validator).start()

    print("Done!")
    return results


def get_bench_configs(path_to_configs: str) -> List[Tuple[str, str, str, str]]:
    if not os.path.exists(path_to_configs) or not os.path.isfile(path_to_configs):
        print(f"Could not read from {path_to_configs}. Does the file exist?")
        raise
    with open(path_to_configs) as fp:
        configs = json.load(fp)
        return configs


def main():
    parser = argparse.ArgumentParser(description="Runs the Nitwit on SV-Benchmark")
    parser.add_argument("-w", "--witnesses", required=True, type=str, help="The directory with unzipped witnesses.")
    parser.add_argument("-e", "--exec", required=True, type=str, help="The Nitwit executable.")
    parser.add_argument("-sv", "--sv_benchmark", required=True, type=str, help="The SV-COMP benchmark source files.")
    parser.add_argument("-to", "--timeout", required=False, type=float, default=300, help="Timeout for a validation.")
    parser.add_argument("-l", "--limit", required=False, type=int, default=None, help="How many configurations to run.")
    parser.add_argument("-p", "--processes", required=False, type=int, default=48, help="Size of the process pool.")
    parser.add_argument("-c", "--config", required=True, type=str, help="The executions configuration file.")

    args = parser.parse_args()
    if not setup_dirs(args.witnesses, args.sv_benchmark, args.exec, args.timeout):
        return 1

    configs = get_bench_configs(args.config)
    if args.limit is not None:
        configs = configs[:args.limit]
    results = run_bench_parallel(configs, args.processes)
    process_results(results, BENCH_RESULTS_HEADER, VALIDATOR_EXECUTABLE, True)


if __name__ == "__main__":
    main()
