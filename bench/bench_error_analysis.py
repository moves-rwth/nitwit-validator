import argparse
import os
import sys
import json
import pandas as pd

from typing import Tuple, List, Optional, Dict

from common.utils import increase_count_in_dict, load_result_files, save_table_to_file

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

COLUMN_INDEX = {
	'status': 0,
	'wit_key': 0,
	'out': 0,
	'err_out': 0,
	'cpu': 0,
	'tool': 0, 
	'source': 0,
	'mem': 0
}

def set_header_index(header: Tuple[str,str,str,str,str,str,str,str]):
	for i in range(len(header)):
		COLUMN_INDEX[header[i]] = i


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

	err_msg_map = {}
	stderr_msg_map = {}
	file_map = {}

	for witness in results:
		increase_count_in_dict(err_msg_map, witness[COLUMN_INDEX['out']])
		if witness[COLUMN_INDEX['out']] in file_map:		
			file_map[witness[COLUMN_INDEX['out']]].append(witness[COLUMN_INDEX['source']])		
		else:
			file_map[witness[COLUMN_INDEX['out']]] = [witness[COLUMN_INDEX['source']]]
			
		for error_msg in witness[COLUMN_INDEX['err_out']]:
			increase_count_in_dict(stderr_msg_map, error_msg)
			if error_msg in file_map:				
				file_map[error_msg].append(witness[COLUMN_INDEX['source']])
			else:
				file_map[error_msg] = [witness[COLUMN_INDEX['source']]]
	
	sort_err_map = {k: v for k, v in sorted(err_msg_map.items(), key=lambda item: item[1])}
	sort_stderr_map = {k: v for k, v in sorted(stderr_msg_map.items(), key=lambda item: item[1])}
	for entry in file_map:
		file_map[entry] = sorted(list(set(file_map[entry])))	


	df1 = pd.DataFrame(sort_err_map.items(), columns=["Error msg.", "count"])
	df2 = pd.DataFrame(sort_stderr_map.items(), columns=["Error msg.", "count"])
	df3 = pd.DataFrame(file_map.items(), columns=["Error msg.", "count"])	
	
	#save_table_to_file(df1.to_latex(), f'{name}_out_msg', TABLE_DIR)
	#save_table_to_file(df2.to_latex(), f'{name}_stderr_out_msg', TABLE_DIR)
	#save_table_to_file(df3.to_latex(), f'{name}_msg_to_file', TABLE_DIR)
	
	if PRINT:	
		print(f"File names {name}:")
		for out, entry_list in file_map.items():
			print(f"{out}: ")
			for entry in entry_list:
				print(f"{entry}, ")

		print(f"Error messages {name}:")
		print_error_msgs(sort_err_map)
		print(f"Standard Error messages {name}:")
		print_error_msgs(sort_stderr_map)
		
	

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
	set_header_index(validated[0])
	analyze_bench_output(badlyparsed[1:], 'badly parsed', '', args.producer)
	analyze_bench_output(nonvalidated[1:], 'non validated', '', args.producer)

if __name__ == "__main__":
	main()
