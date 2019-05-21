import argparse
import os
import sys
import json
from typing import Tuple, List, Optional, Dict

from common.utils import emplace_in_dict, load_result_files

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
	245: 'bad function definition',
	246: 'identifier already defined',
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


def analyze_bench_output(results: list, name: str, search_string: str):
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
		emplace_in_dict(result_map, witness[0])
		with open(os.path.join(WITNESS_INFO_BY_WITNESS_HASH_DIR, witness[1]), 'r') as fp:
			info_jObj = json.load(fp)
			if 'producer' in info_jObj:
				emplace_in_dict(prod_map, info_jObj['producer'])
			else:
				unknown = unknown + 1

			pf = str(info_jObj['programfile'])
			if pf.find(search_string) == -1:
				false_positives = false_positives + 1
		emplace_in_dict(err_msg_map, f"({witness[0]}) {witness[2]}")

	print('-' * 20 + ' ', name.capitalize(), ' ' + '-' * 20)
	print(f"Producer summary: {prod_map}")
	print(f"Result summary:")
	print_result_map(result_map)
	print(f"Details:")
	print_error_msgs(err_msg_map)
	if unknown > 0:
		print(f"Unknown producers for {unknown} witnesses.")
	fp_rate = 0 if len(results) == 0 else false_positives / len(results) * 100
	print(f"Incorrect results for {name}: {false_positives}, i.e. {fp_rate}%.")
	print(f"In total {name} {len(results)}.")


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
	# parser.add_argument("-dv", "--dvalidated", required=False, type=str, default=None,
	#                     help="The file with info about validated witnesses of the second result.")
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
	analyze_bench_output(validated, 'validated', '_false-unreach-call')
	analyze_bench_output(nonvalidated, 'non-validated', '_true-unreach-call')
	analyze_bench_output(badlyparsed, 'badly parsed', '')


# if args.validated and args.dvalidated:
# 	not_in_d, not_in_v = get_differences(load_result_files(args.validated),
# 	                                     load_result_files(args.dvalidated))
# 	print(f"Witnesses not in {args.validated}: {len(not_in_v)}")
# 	analyze_bench_output(not_in_v, 'validated by second, but not by first', '_false-unreach-call')
# 	print('=' * 80)
# 	print(f"Witnesses not in {args.dvalidated}: {len(not_in_d)}")
# 	analyze_bench_output(not_in_d, 'validated by first, but not by second', '_false-unreach-call')
# 	print('=' * 80)
# print(get_info_files_for_producer(not_in_d, 'CPAchecker 1.7-svn b8d6131600+'))

# if args.nonvalidated:
# 	analyze_bench_output(args.nonvalidated, 'non-validated', '_true-unreach-call')
#
# if args.badlyparsed:
# 	analyze_bench_output(args.badlyparsed, 'badly parsed', '')


if __name__ == "__main__":
	main()
