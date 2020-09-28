import argparse
import os
import sys
import json
import pandas as pd

from typing import Tuple, List, Optional, Dict

from common.utils import load_result_files

WITNESSES_BY_PROGRAM_HASH_DIR = "witnessListByProgramHashJSON"
WITNESS_INFO_BY_WITNESS_HASH_DIR = "witnessInfoByHash"
WITNESS_FILE_BY_HASH_DIR = "witnessFileByHash"

TABLE_DIR = f'./output/tables19'

PRINT = True

EXIT_CODE_DICT = {
	0: 'validated',
	2: 'witness parse error',
	3: 'usage error',
	4: 'unspecified error, probably parsing C',
	5: '__VERIFIER_error not called',
	-6: 'PicoC error',
	6: 'PicoC error',
	9: 'killed',
	-11: 'witness parse error',
	11: 'witness parse error',
	240: 'no witness code',
	241: 'witness got to sink',
	242: 'program finished before violation node reached',
	243: 'witness got into an illegal state',
	244: 'identifier undefined',
	245: '__VERIFIER_error called, but witness not in violation state',
	246: 'identifier already defined',
	247: 'unsupported nondet operation',
	248: 'assertion failed',
	249: 'bad function definition',
	250: 'witness in violation state, though no violation occurred',
	251: 'out of memory'
}

def setup_dirs(dir: str) -> bool:
	if not os.path.exists(dir) or not os.path.isdir(dir):
		print(f"The directory {dir} doesn't exist or is not a directory.", file=sys.stderr)
		return False

	global WITNESS_INFO_BY_WITNESS_HASH_DIR, WITNESSES_BY_PROGRAM_HASH_DIR, WITNESS_FILE_BY_HASH_DIR, SV_BENCHMARK_DIR
	WITNESS_INFO_BY_WITNESS_HASH_DIR = os.path.join(dir, WITNESS_INFO_BY_WITNESS_HASH_DIR)
	WITNESSES_BY_PROGRAM_HASH_DIR = os.path.join(dir, WITNESSES_BY_PROGRAM_HASH_DIR)
	WITNESS_FILE_BY_HASH_DIR = os.path.join(dir, WITNESS_FILE_BY_HASH_DIR)

	if not (os.path.exists(WITNESS_INFO_BY_WITNESS_HASH_DIR) and os.path.isdir(WITNESS_INFO_BY_WITNESS_HASH_DIR) and
	        os.path.exists(WITNESS_FILE_BY_HASH_DIR) and os.path.isdir(WITNESS_FILE_BY_HASH_DIR) and
	        os.path.exists(WITNESSES_BY_PROGRAM_HASH_DIR) and os.path.isdir(WITNESSES_BY_PROGRAM_HASH_DIR)):
		print(f"The directory {dir} doesn't contain the necessary directories.", file=sys.stderr)
		return False
	print("Directories found!")
	return True

def analyze_bench_output(results: list, name: str, search_string: str, producer: str):
	'''
	Analyzes the producers and wrong results of the C Witness Validator
	:param results: List of witness info files.
	:param name: Type of output.
	:param search_string: The string that has to occur in programfile field of the info file
	:return:
	'''

	if results is None:
		return

	counter = 0

	for witness in results:
		counter = counter + 1

	if PRINT:
		print(f"File: {name}, Number of Files: {counter}")
		
	

def main():
	parser = argparse.ArgumentParser(description="Runs the Nitwit on SV-Benchmark")
	parser.add_argument("-w", "--witnesses", required=True, type=str, help="The directory with unzipped witnesses.")
	parser.add_argument("-r", "--results", required=True, type=str, default=None,
	                    help="The directory with resulting files about validated witnesses.")
	parser.add_argument("-p", "--producer", required=False, type=str, default=None,
	                    help="Restrict analysis to this producer.")

	args = parser.parse_args()
	if not setup_dirs(args.witnesses):
		return 1

	validated, nonvalidated, badlyparsed = load_result_files(args.results)
	analyze_bench_output(badlyparsed[1:], 'badly parsed', '', args.producer)
	analyze_bench_output(nonvalidated[1:], 'non validated', '', args.producer)

if __name__ == "__main__":
	main()
