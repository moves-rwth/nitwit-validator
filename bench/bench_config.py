import argparse
import os
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


def setup_dirs(dir: str, sv_dir: str) -> bool:
    if not os.path.exists(dir) or not os.path.isdir(dir):
        print(f"The directory {dir} doesn't exist or is not a directory.", file=sys.stderr)
        return False

    global WITNESS_INFO_BY_WITNESS_HASH_DIR, WITNESSES_BY_PROGRAM_HASH_DIR, WITNESS_FILE_BY_HASH_DIR, SV_BENCHMARK_DIR
    WITNESS_INFO_BY_WITNESS_HASH_DIR = os.path.join(dir, WITNESS_INFO_BY_WITNESS_HASH_DIR)
    WITNESSES_BY_PROGRAM_HASH_DIR = os.path.join(dir, WITNESSES_BY_PROGRAM_HASH_DIR)
    WITNESS_FILE_BY_HASH_DIR = os.path.join(dir, WITNESS_FILE_BY_HASH_DIR)
    SV_BENCHMARK_DIR = os.path.join(sv_dir, 'c')

    if not (os.path.exists(WITNESS_INFO_BY_WITNESS_HASH_DIR) and os.path.isdir(WITNESS_INFO_BY_WITNESS_HASH_DIR) and
            os.path.exists(WITNESS_FILE_BY_HASH_DIR) and os.path.isdir(WITNESS_FILE_BY_HASH_DIR) and
            os.path.exists(WITNESSES_BY_PROGRAM_HASH_DIR) and os.path.isdir(WITNESSES_BY_PROGRAM_HASH_DIR)):
        print(f"The directory {dir} doesn't contain the necessary directories.", file=sys.stderr)
        return False
    if not (os.path.exists(SV_BENCHMARK_DIR) and os.path.isdir(SV_BENCHMARK_DIR)):
        print(f"The directory {sv_dir} doesn't exist or isn't a directory.", file=sys.stderr)
        return False
    print("Directories found!")
    return True


def get_bench_params(exec_limit: Union[int, None], should_include: Callable[[str], bool],
                     should_exclude: Callable[[str], bool]) -> List[Tuple[str, str, str]]:
    sv_bench_not_found = 0
    configs_to_run = []

    with os.scandir(WITNESS_INFO_BY_WITNESS_HASH_DIR) as it:
        for entry in it:
            if entry.name.startswith('.') or not entry.is_file() \
                    or not should_include(os.path.basename(entry.name)) \
                    or should_exclude(os.path.basename(entry.name)):
                continue
            with open(entry.path) as f:
                jObj = json.load(f)
                try:
                    if 'programfile' in jObj and 'programhash' in jObj:
                        programfile = str(jObj['programfile'])
                        if not str(jObj['specification']) == \
                                       "CHECK( init(main()), LTL(G ! call(__VERIFIER_error())) )" or \
                                ('witness-type' in jObj and not jObj['witness-type'] == 'violation_witness'):
                            continue  # not a reachability C verification file
                        sv_regexp_location = programfile.find("sv-benchmarks/c/")
                        if not sv_regexp_location == -1:
                            path_to_source_code = os.path.join(SV_BENCHMARK_DIR, programfile[
                                                                                 sv_regexp_location + len(
                                                                                     "sv-benchmarks/c/"):])
                            if 'witness-sha256' in jObj:
                                # check if file in witnessFileByHash exists
                                path_to_witness_file = \
                                    os.path.join(WITNESS_FILE_BY_HASH_DIR, f'{jObj["witness-sha256"]}.graphml')
                                if not os.path.isfile(path_to_witness_file):
                                    print(f"{path_to_witness_file} did not exist")
                                    continue

                                if os.path.isfile(path_to_source_code):
                                    # run the validator with the found witness and source code
                                    configs_to_run.append((path_to_witness_file, path_to_source_code, entry.name))
                                else:
                                    # print(f"SV-COMP file {path_to_source_code} not found!")
                                    sv_bench_not_found = sv_bench_not_found + 1

                except Exception as e:
                    print(f"Error while processing {entry.name}: {e}")
            if exec_limit is not None and len(configs_to_run) >= exec_limit:
                break

        print(f"Prepared {len(configs_to_run)} SV-COMP files with safety "
              f"properties checking. Not found {sv_bench_not_found}.")
        print("-------------------------------------------------------------------")
    return configs_to_run


def get_result_set(path: str) -> Set[str]:
    if not os.path.isfile(path):
        print(f"Cannot load configuration from {path}")
        exit(1)
    with open(path) as rp:
        jObj = json.load(rp)
        return set([result[1] for result in jObj])


def get_benchmark_file_path(benchmark: str):
    b = os.path.join(SV_BENCHMARK_DIR, benchmark)
    if not os.path.isfile(b):
        if benchmark.startswith('elevator_spec') or benchmark.startswith('email_spec') or benchmark.startswith('minepump_spec'):
            b = os.path.join(SV_BENCHMARK_DIR, 'product-lines', benchmark)
            if os.path.isfile(b):
                return b
        return Exception(f"File not found: {benchmark}")
    else:
        return b


def extract_config_from_extracted_data(json_data: str, limit: int, should_include, should_exclude) -> List[Tuple[str, str, str, str]]:
    if not os.path.isfile(json_data):
        raise Exception("Data file doesn't exist.")
    with open(json_data) as fp:
        witnesses = json.load(fp)['byWitnessHash']
    result = [(os.path.join(WITNESS_FILE_BY_HASH_DIR, f"{w}.graphml"),
               get_benchmark_file_path(v['benchmark']),
               f"{w}.json",
               v['tool'])
              for w, v in witnesses.items()
              if str(v['benchmark']).find('false-unreach-call') != -1 and should_include(f"{w}.json") and not should_exclude(f"{w}.json")]

    if limit is not None:
        result = result[:limit]
    return result


def main():
    parser = argparse.ArgumentParser(description="Prepares configurations for Nitwit on SV-Benchmark")
    parser.add_argument("-w", "--witnesses", required=True, type=str, help="The directory with unzipped witnesses.")
    parser.add_argument("-sv", "--sv_benchmark", required=True, type=str, help="The SV-COMP benchmark source files.")
    parser.add_argument("-l", "--limit", required=False, type=int, default=None,
                        help="Limit of the number of executions")
    parser.add_argument("-rs", "--restrict", required=False, type=str,
                        help="Run only witnesses present in the provided "
                             "JSON result from a previous run.")
    parser.add_argument("-ex", "--exclude", required=False, type=str,
                        help="Exclude these previous results from the bench.")
    parser.add_argument("-ed", "--extracted_data", required=False, type=str, help="JSON file with info on SV-COMP witnesses.")
    parser.add_argument("input", type=str, help="The output file path.")

    args = parser.parse_args()
    if not setup_dirs(args.witnesses, args.sv_benchmark):
        return 1

    should_include = lambda s: True
    should_exclude = lambda s: False
    if args.restrict is not None:
        restricted = get_result_set(args.restrict)
        should_include = lambda s: s in restricted
    if args.exclude is not None:
        excluded = get_result_set(args.exclude)
        should_exclude = lambda s: s in excluded

    if args.extracted_data is not None:
        configs = extract_config_from_extracted_data(args.extracted_data, args.limit, should_include, should_exclude)
    else:
        configs = get_bench_params(args.limit, should_include, should_exclude)
    with open(args.input, 'w') as fp:
        json.dump(configs, fp)


if __name__ == "__main__":
    main()
