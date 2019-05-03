import argparse
import os
import sys
import json
from typing import Tuple, List, Optional

WITNESSES_BY_PROGRAM_HASH_DIR = "witnessListByProgramHashJSON"
WITNESS_INFO_BY_WITNESS_HASH_DIR = "witnessInfoByHash"
WITNESS_FILE_BY_HASH_DIR = "witnessFileByHash"


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

def load_result_file(results: str) -> Optional[List[str]]:
	if not (os.path.exists(results) and os.path.isfile(results)):
		print("Cannot load output file with info about witnesses.")
		return None
	with open(results, 'r') as fp:
		valid_jObj = json.load(fp)
	return list(valid_jObj)

def analyze_bench_output(results: List[str], name: str, search_string: str):
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
	unknown = 0
	false_positives = 0

	for w in results:
		with open(os.path.join(WITNESS_INFO_BY_WITNESS_HASH_DIR, w), 'r') as fp:
			info_jObj = json.load(fp)
			if 'producer' in info_jObj:
				producer = info_jObj['producer']
				if producer in prod_map:
					prod_map[producer] = prod_map[producer] + 1
				else:
					prod_map[producer] = 1
			else:
				unknown = unknown + 1

			pf = str(info_jObj['programfile'])
			if pf.find(search_string) == -1:
				false_positives = false_positives + 1

	print('-' * 40)
	print(prod_map)
	print(f"Unknown producers for {unknown} witnesses.")
	fp_rate = 0 if len(results) == 0 else false_positives / len(results) * 100
	print(f"Incorrect results for {name}: {false_positives}, i.e. {fp_rate}%.")
	print(f"In total {name} {len(results)}.")


def get_differences(a: List[str], b: List[str]) -> Tuple[List[str], List[str]]:
	sa, sb = set(a), set(b)
	return list(sa.difference(sb)), list(sb.difference(sa))


def main():
	parser = argparse.ArgumentParser(description="Runs the CWValidator on SV-Benchmark")
	parser.add_argument("-w", "--witnesses", required=True, type=str, help="The directory with unzipped witnesses.")
	parser.add_argument("-v", "--validated", required=False, type=str, default=None,
	                    help="The file with info about validated witnesses.")
	parser.add_argument("-dv", "--dvalidated", required=False, type=str, default=None,
	                    help="The file with info about validated witnesses of the second result.")
	parser.add_argument("-nv", "--nonvalidated", required=False, type=str, default=None,
	                    help="The file with info about non-validated witnesses.")
	parser.add_argument("-bp", "--badlyparsed", required=False, type=str, default=None,
	                    help="The file with info about badly parsed witnesses.")
	parser.add_argument("-d", "--differences", required=False, action='store_true', help="Show the differences between two results.")
	# parser.add_argument("-c", "--config", required=True, type=str, help="The verifier configuration file.")

	args = parser.parse_args()
	if not setup_dirs(args.witnesses):
		return 1

	if not args.differences:
		if args.validated:
			analyze_bench_output(load_result_file(args.validated), 'validated', '_false-unreach-call')

		if args.nonvalidated:
			analyze_bench_output(load_result_file(args.nonvalidated), 'non-validated', '_true-unreach-call')

		if args.badlyparsed:
			analyze_bench_output(load_result_file(args.badlyparsed), 'badly parsed', '')
	else:
		if args.validated and args.dvalidated:
			not_in_d, not_in_v = get_differences(load_result_file(args.validated),
			                                      load_result_file(args.dvalidated))
			print(f"Witnesses not in {args.validated}: {len(not_in_v)}")
			analyze_bench_output(not_in_v, 'validated by second, but not by first', '_false-unreach-call')
			print('='*80)
			print(f"Witnesses not in {args.dvalidated}: {len(not_in_d)}")
			analyze_bench_output(not_in_d, 'validated by first, but not by second', '_false-unreach-call')
			print('='*80)

		# if args.nonvalidated:
		# 	analyze_bench_output(args.nonvalidated, 'non-validated', '_true-unreach-call')
		#
		# if args.badlyparsed:
		# 	analyze_bench_output(args.badlyparsed, 'badly parsed', '')


if __name__ == "__main__":
	main()