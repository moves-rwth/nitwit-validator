import argparse
import os
import sys
import json
from typing import Tuple, List, Optional, Dict

from common.utils import increase_count_in_dict, load_result_files

WITNESSES_BY_PROGRAM_HASH_DIR = "witnessListByProgramHashJSON"
WITNESS_INFO_BY_WITNESS_HASH_DIR = "witnessInfoByHash"
WITNESS_FILE_BY_HASH_DIR = "witnessFileByHash"

EXIT_CODE_DICT = {
	0: 'validated',
	2: 'witness parse error',
	3: 'usage error',
	4: 'unspecified error, probably parsing C',
	5: '__VERIFIER_error not called',
	9: 'killed',
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
	250: 'witness in violation state, though no violation occurred'
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


def print_result_map(m: Dict[int, int]):
	for k, v in m.items():
		print(f"\t({k}) {EXIT_CODE_DICT[k]}: {v}")


def print_error_msgs(m: Dict[str, int]):
	for k, v in m.items():
		print(f"\t{k}: {v}")


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

	prod_map = {}
	result_map = {}
	err_msg_map = {}
	unknown = 0
	false_positives = 0

	for witness in results:
		with open(os.path.join(WITNESS_INFO_BY_WITNESS_HASH_DIR, witness[1]), 'r') as fp:
			info_jObj = json.load(fp)
			if producer is not None and not str(info_jObj['producer']).startswith(producer):
				continue
			increase_count_in_dict(result_map, witness[0])
			if 'producer' in info_jObj:
				increase_count_in_dict(prod_map, info_jObj['producer'])
			else:
				unknown = unknown + 1

			pf = str(info_jObj['programfile'])
			if pf.find(search_string) == -1:
				false_positives = false_positives + 1
		increase_count_in_dict(err_msg_map, f"({witness[0]}) {witness[2]}")

	n_results = sum(result_map.values())
	print('-' * 20 + ' ', name.capitalize(), ' ' + '-' * 20)
	print(f"Producer summary: {prod_map}")
	print(f"Result summary:")
	print_result_map(result_map)
	print(f"Details:")
	print_error_msgs(err_msg_map)
	if unknown > 0:
		print(f"Unknown producers for {unknown} witnesses.")
	fp_rate = 0 if len(results) == 0 else false_positives / n_results * 100
	print(f"Incorrect results for {name}: {false_positives}, i.e. {fp_rate}%.")
	print(f"In total {name} {n_results}.")


def get_differences(a: List[str], b: List[str]) -> Tuple[List[str], List[str]]:
	sa, sb = set(a), set(b)
	return list(sa.difference(sb)), list(sb.difference(sa))


def get_info_files_for_producer(results: List[str], producer: str) -> List[str]:
	if results is None:
		return []

	info_files = []
	for w in results:
		with open(os.path.join(WITNESS_INFO_BY_WITNESS_HASH_DIR, w), 'r') as fp:
			info_jObj = json.load(fp)
			if 'producer' in info_jObj and info_jObj['producer'] == producer:
				info_files.append(w)
	return info_files


def main():
	parser = argparse.ArgumentParser(description="Runs the CWValidator on SV-Benchmark")
	parser.add_argument("-w", "--witnesses", required=True, type=str, help="The directory with unzipped witnesses.")
	parser.add_argument("-r", "--results", required=True, type=str, default=None,
	                    help="The directory with resulting files about validated witnesses.")
	parser.add_argument("-p", "--producer", required=False, type=str, default=None,
	                    help="Restrict analysis to this producer.")
	# parser.add_argument("-nv", "--nonvalidated", required=False, type=str, default=None,
	#                     help="The file with info about non-validated witnesses.")
	# parser.add_argument("-bp", "--badlyparsed", required=False, type=str, default=None,
	#                     help="The file with info about badly parsed witnesses.")
	# parser.add_argument("-d", "--differences", required=False, action='store_true', help="Show the differences between two results.")
	# parser.add_argument("-c", "--config", required=True, type=str, help="The verifier configuration file.")

	args = parser.parse_args()
	if not setup_dirs(args.witnesses):
		return 1

	validated, nonvalidated, badlyparsed = load_result_files(args.results)
	analyze_bench_output(validated, 'validated', '_false-unreach-call', args.producer)
	analyze_bench_output(nonvalidated, 'non-validated', '_true-unreach-call', args.producer)
	analyze_bench_output(badlyparsed, 'badly parsed', '', args.producer)


if __name__ == "__main__":
	main()
