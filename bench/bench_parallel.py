import argparse
import os
import resource
import sys
import json
from typing import Union, List, Tuple, Callable, Set
import multiprocessing
import subprocess

from common.utils import process_results

WITNESSES_BY_PROGRAM_HASH_DIR = "witnessListByProgramHashJSON"
WITNESS_INFO_BY_WITNESS_HASH_DIR = "witnessInfoByHash"
WITNESS_FILE_BY_HASH_DIR = "witnessFileByHash"
SV_BENCHMARK_DIR = ""
VALIDATOR_EXECUTABLE = ""
EXECUTION_TIMEOUT = 0


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


def run_validator(config: Tuple[str, str, str]) -> Tuple[int, str, str, float]:
    witness, source, info_file = config
    children_before = resource.getrusage(resource.RUSAGE_CHILDREN)
    with subprocess.Popen([VALIDATOR_EXECUTABLE, witness, source], shell=False,
                          stdout=subprocess.PIPE,
                          stderr=subprocess.DEVNULL) as process:
        errmsg = ''
        try:
            out, _ = process.communicate(timeout=EXECUTION_TIMEOUT)
            if process.returncode != 0 and out is not None:
                errmsg = out.splitlines()
                errmsg = '' if len(errmsg) < 3 else str(errmsg[-1])
                pos = errmsg.find('#')
                if pos != -1:
                    errmsg = errmsg[pos + 1:]
                else:
                    errmsg = str(out).strip()
        except subprocess.TimeoutExpired:
            process.kill()
        finally:
            if process.poll() is None:
                print(f"Process {process.pid} still running!")
                process.kill()
                _, _ = process.communicate()

        children = resource.getrusage(resource.RUSAGE_CHILDREN)
        return process.returncode, info_file, errmsg, children.ru_utime + children.ru_stime - (children_before.ru_utime + children_before.ru_stime)


def run_bench_parallel(configs: List[Tuple[str, str, str]]) -> List[Tuple[int, str, str, float]]:
    with multiprocessing.Pool(10) as pool:
        results = pool.map(run_validator, configs)
    return results


def get_bench_configs(path_to_configs: str) -> List[Tuple[str, str, str]]:
    if not os.path.exists(path_to_configs) or not os.path.isfile(path_to_configs):
        print(f"Could not read from {path_to_configs}. Does the file exist?")
        raise
    with open(path_to_configs) as fp:
        configs = json.load(fp)
        return configs


def main():
    parser = argparse.ArgumentParser(description="Runs the CWValidator on SV-Benchmark")
    parser.add_argument("-w", "--witnesses", required=True, type=str, help="The directory with unzipped witnesses.")
    parser.add_argument("-e", "--exec", required=True, type=str, help="The CWValidator executable.")
    parser.add_argument("-sv", "--sv_benchmark", required=True, type=str, help="The SV-COMP benchmark source files.")
    parser.add_argument("-to", "--timeout", required=False, type=float, default=300, help="Timeout for a validation.")
    parser.add_argument("-l", "--limit", required=False, type=int, default=None, help="How many configurations to run.")
    parser.add_argument("-c", "--config", required=True, type=str, help="The executions configuration file.")

    args = parser.parse_args()
    if not setup_dirs(args.witnesses, args.sv_benchmark, args.exec, args.timeout):
        return 1

    configs = get_bench_configs(args.config)
    if args.limit is not None:
        configs = configs[:args.limit]
    results = run_bench_parallel(configs)
    process_results(results, VALIDATOR_EXECUTABLE, True)


if __name__ == "__main__":
    main()
