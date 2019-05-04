import argparse
import os
import sys
import json
from typing import Union, List, Tuple
import common.utils
import subprocess

WITNESSES_BY_PROGRAM_HASH_DIR = "witnessListByProgramHashJSON"
WITNESS_INFO_BY_WITNESS_HASH_DIR = "witnessInfoByHash"
WITNESS_FILE_BY_HASH_DIR = "witnessFileByHash"
SV_BENCHMARK_DIR = ""
VALIDATOR_EXECUTABLE = ""
EXECUTION_TIMEOUT = 0


def run_validator(config: Tuple[str, str, str]) -> Tuple[int, str]:
	witness, source, info_file = config
	with subprocess.Popen(f"{VALIDATOR_EXECUTABLE} {witness} {source}", shell=True,
	                      stdout=subprocess.PIPE,
	                      stderr=subprocess.PIPE) as process:
		try:
			outs, errs = process.communicate(timeout=EXECUTION_TIMEOUT)

			print(f"{'-' * 20}Stdout{'-' * 20}")
			print(outs.decode("utf-8"))
			print('=' * 46)
			print(f"{'-' * 20}Stderr{'-' * 20}")
			print(errs.decode("utf-8"))
		except subprocess.TimeoutExpired:
			process.kill()
		# outs, errs = process.communicate()

	return process.returncode, info_file


def setup_dirs(dir: str, sv_dir: str, executable: str, timeout: float) -> bool:
	if not os.path.exists(dir) or not os.path.isdir(dir):
		print(f"The directory {dir} doesn't exist or is not a directory.", file=sys.stderr)
		return False

	global WITNESS_INFO_BY_WITNESS_HASH_DIR, WITNESSES_BY_PROGRAM_HASH_DIR, WITNESS_FILE_BY_HASH_DIR, SV_BENCHMARK_DIR, VALIDATOR_EXECUTABLE, EXECUTION_TIMEOUT
	WITNESS_INFO_BY_WITNESS_HASH_DIR = os.path.join(dir, WITNESS_INFO_BY_WITNESS_HASH_DIR)
	WITNESSES_BY_PROGRAM_HASH_DIR = os.path.join(dir, WITNESSES_BY_PROGRAM_HASH_DIR)
	WITNESS_FILE_BY_HASH_DIR = os.path.join(dir, WITNESS_FILE_BY_HASH_DIR)
	SV_BENCHMARK_DIR = os.path.join(sv_dir, 'c')
	VALIDATOR_EXECUTABLE = executable
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


def run_single_config(witness_info_file: str) -> Tuple[int, str]:
	witness_info_file = os.path.join(WITNESS_INFO_BY_WITNESS_HASH_DIR, witness_info_file)
	if not os.path.isfile(witness_info_file):
		raise FileNotFoundError(witness_info_file)
	with open(witness_info_file) as f:
		jObj = json.load(f)
		print(jObj)
		if 'witness-sha256' in jObj:
			# check if file in witnessFileByHash exists
			path_to_witness_file = \
				os.path.join(WITNESS_FILE_BY_HASH_DIR, f'{jObj["witness-sha256"]}.graphml')
			if not os.path.isfile(path_to_witness_file):
				print(f"{path_to_witness_file} did not exist")
				raise FileNotFoundError(path_to_witness_file)

			if 'programfile' in jObj and 'programhash' in jObj:
				programfile = str(jObj['programfile'])
				if not programfile.endswith(".c") \
						or not str(
					jObj['specification']) == "CHECK( init(main()), LTL(G ! call(__VERIFIER_error())) )":
					raise Exception("Not a reachability C verification file")

				sv_regexp_location = programfile.find("sv-benchmarks/c/")
				if not sv_regexp_location == -1:
					path_to_source_code = os.path.join(SV_BENCHMARK_DIR, programfile[
					                                                     sv_regexp_location + len(
						                                                     "sv-benchmarks/c/"):])
					if os.path.isfile(path_to_source_code):
						# run the validator with the found witness and source code
						return run_validator((path_to_witness_file, path_to_source_code, witness_info_file))
					else:
						raise Exception(f"SV-COMP file {path_to_source_code} not found!")
				else:
					raise Exception("No programfile property")
		else:
			raise Exception("No witness-sha256 property")


def main():
	parser = argparse.ArgumentParser(description="Runs the CWValidator on SV-Benchmark")
	parser.add_argument("-w", "--witnesses", required=True, type=str, help="The directory with unzipped witnesses.")
	parser.add_argument("-e", "--exec", required=True, type=str, help="The CWValidator executable.")
	parser.add_argument("-sv", "--sv_benchmark", required=True, type=str, help="The SV-COMP benchmark source files.")
	parser.add_argument("-to", "--timeout", required=False, type=float, default=300, help="Timeout for a validation.")
	parser.add_argument("-f", "--filename", required=True, type=str, help="The witness info filename to run.")

	args = parser.parse_args()
	if not setup_dirs(args.witnesses, args.sv_benchmark, args.exec, args.timeout):
		return 1

	results = run_single_config(args.filename)
	common.utils.process_results([results], VALIDATOR_EXECUTABLE, False)


if __name__ == "__main__":
	main()
