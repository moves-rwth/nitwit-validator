import argparse
import os
import sys
import json

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


def analyze_bench_output_validated(validated: str):
	if not (os.path.exists(validated) and os.path.isfile(validated)):
		print("Cannot load output file with info about witnesses.")
		return
	prod_map = {}
	unknown = 0
	false_positives = 0
	with open(validated, 'r') as fp:
		valid_jObj = json.load(fp)

	for w in valid_jObj:
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
			if pf.find('_false-unreach-call') == -1:
				false_positives = false_positives + 1
	print(prod_map)
	print(f"Unknown producers at {unknown} witnesses.")
	print(f"False positives: {false_positives}, i.e. {false_positives/len(valid_jObj)}%")
	print(f"In total validated {len(valid_jObj)}.")

def main():
	parser = argparse.ArgumentParser(description="Runs the CWValidator on SV-Benchmark")
	parser.add_argument("-w", "--witnesses", required=True, type=str, help="The directory with unzipped witnesses.")
	parser.add_argument("-v", "--validated", required=True, type=str, default=None,
	                    help="The file with info about validated witnesses.")
	# parser.add_argument("-c", "--config", required=True, type=str, help="The verifier configuration file.")

	args = parser.parse_args()
	if not setup_dirs(args.witnesses):
		return 1

	analyze_bench_output_validated(args.validated)


if __name__ == "__main__":
	main()
