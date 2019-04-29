import json
import argparse
import os

import sys

WITNESSES_BY_PROGRAM_HASH_DIR = "witnessListByProgramHashJSON"
WITNESS_INFO_BY_WITNESS_HASH_DIR = "witnessInfoByHash"
WITNESS_FILE_BY_HASH_DIR = "witnessFileByHash"


def setup_dirs(dir: str) -> bool:
	if not os.path.exists(dir) or not os.path.isdir(dir):
		print(f"The directory {dir} doesn't exist or is not a directory.", file=sys.stderr)
		return False

	global WITNESS_INFO_BY_WITNESS_HASH_DIR, WITNESSES_BY_PROGRAM_HASH_DIR, WITNESS_FILE_BY_HASH_DIR
	WITNESS_INFO_BY_WITNESS_HASH_DIR = os.path.join(dir, WITNESS_INFO_BY_WITNESS_HASH_DIR)
	WITNESSES_BY_PROGRAM_HASH_DIR = os.path.join(dir, WITNESSES_BY_PROGRAM_HASH_DIR)
	WITNESS_FILE_BY_HASH_DIR = os.path.join(dir, WITNESS_FILE_BY_HASH_DIR)

	if not (os.path.exists(WITNESS_INFO_BY_WITNESS_HASH_DIR) and os.path.isdir(WITNESS_INFO_BY_WITNESS_HASH_DIR) and
	        os.path.exists(WITNESS_FILE_BY_HASH_DIR) and os.path.isdir(WITNESS_FILE_BY_HASH_DIR) and
	        os.path.exists(WITNESSES_BY_PROGRAM_HASH_DIR) and os.path.isdir(WITNESSES_BY_PROGRAM_HASH_DIR)):
		print(f"The directory {dir} doesn't contain the necessary directories.", file=sys.stderr)
		return False
	print("Directory found!")
	return True


def filter_violation_witnesses():
	deleted = 0
	erroneous = 0
	not_existent = 0
	with os.scandir(WITNESS_INFO_BY_WITNESS_HASH_DIR) as it:
		for entry in it:
			if entry.name.startswith('.') or not entry.is_file():
				print(f"{entry.name} is not a file.")
				continue
			with open(entry.path) as f:
				jObj = json.load(f)
				try:
					if 'witness-type' in jObj:
						if not jObj['witness-type'] == 'violation_witness':
							# delete the file in witnessFileByHash
							path_to_witness_file = \
								os.path.join(WITNESS_FILE_BY_HASH_DIR, f'{jObj["witness-sha256"]}.graphml')
							if os.path.isfile(path_to_witness_file):
								os.remove(path_to_witness_file)
								deleted = deleted + 1
							else:
								not_existent = not_existent + 1

					else:
						erroneous = erroneous + 1
						path_to_witness_file = \
							os.path.join(WITNESS_FILE_BY_HASH_DIR, f'{jObj["witness-sha256"]}.graphml')
						if os.path.isfile(path_to_witness_file):
							os.remove(path_to_witness_file)
							deleted = deleted + 1
						else:
							not_existent = not_existent + 1
						continue
				except Exception as e:
					print(f"Error while processing {entry.name}: {e}")

	print(f"Deleted {deleted} files and found {erroneous} erroneous files. {not_existent} files did not exist.")


def remove_non_matching_info_files():
	deleted = 0
	erroneous = 0
	existent = 0
	with os.scandir(WITNESS_INFO_BY_WITNESS_HASH_DIR) as it:
		for entry in it:
			if entry.name.startswith('.') or not entry.is_file():
				print(f"{entry.name} is not a file.")
				continue
			with open(entry.path) as f:
				jObj = json.load(f)
				try:
					if 'witness-sha256' in jObj:
						# check if file in witnessFileByHash exists
						path_to_witness_file = \
							os.path.join(WITNESS_FILE_BY_HASH_DIR, f'{jObj["witness-sha256"]}.graphml')
						if not os.path.isfile(path_to_witness_file):
							os.remove(os.path.join(WITNESS_INFO_BY_WITNESS_HASH_DIR, entry.name))
							deleted = deleted + 1
						else:
							existent = existent + 1

					else:
						erroneous = erroneous + 1
						path_to_witness_file = \
							os.path.join(WITNESS_INFO_BY_WITNESS_HASH_DIR, entry.name)
						if os.path.isfile(path_to_witness_file):
							# os.remove(path_to_witness_file)
							deleted = deleted + 1
						else:
							print(f"Info file: {entry.name} did not exist.")
						continue
				except Exception as e:
					print(f"Error while processing {entry.name}: {e}")

		print(f"Deleted {deleted} files and found {erroneous} erroneous files. {existent} files existed.")


def count_producers():
	info_without_proc = 0
	prod_map = {}
	with os.scandir(WITNESS_INFO_BY_WITNESS_HASH_DIR) as it:
		for entry in it:
			if entry.name.startswith('.') or not entry.is_file():
				print(f"{entry.name} is not a file.")
				continue
			with open(entry.path) as f:
				jObj = json.load(f)
				if 'producer' in jObj:
					producer = jObj['producer']
					if producer in prod_map:
						prod_map[producer] = prod_map[producer] + 1
					else:
						prod_map[producer] = 1
				else:
					info_without_proc = info_without_proc + 1

	print(prod_map)
	print(f"Witnesses without producer info: {info_without_proc}.")


def c_files_count_producers():
	info_without_proc = 0
	prod_map = {}
	with os.scandir(WITNESS_INFO_BY_WITNESS_HASH_DIR) as it:
		for entry in it:
			if entry.name.startswith('.') or not entry.is_file():
				print(f"{entry.name} is not a file.")
				continue
			with open(entry.path) as f:
				jObj = json.load(f)
				if 'producer' in jObj:
					producer = jObj['producer']
					if not str(jObj['programfile']).endswith(".c"):
						continue
					if producer in prod_map:
						prod_map[producer] = prod_map[producer] + 1
					else:
						prod_map[producer] = 1
				else:
					info_without_proc = info_without_proc + 1

	print(prod_map)
	print(f"Witnesses without producer info: {info_without_proc}.")


def reach_c_files_count_producers():
	info_without_proc = 0
	prod_map = {}
	with os.scandir(WITNESS_INFO_BY_WITNESS_HASH_DIR) as it:
		for entry in it:
			if entry.name.startswith('.') or not entry.is_file():
				print(f"{entry.name} is not a file.")
				continue
			with open(entry.path) as f:
				jObj = json.load(f)
				if 'producer' in jObj:
					producer = jObj['producer']
					if not str(jObj['programfile']).endswith(".c") \
							or not str(
						jObj['specification']) == "CHECK( init(main()), LTL(G ! call(__VERIFIER_error())) )":
						continue
					if producer in prod_map:
						prod_map[producer] = prod_map[producer] + 1
					else:
						prod_map[producer] = 1
				else:
					info_without_proc = info_without_proc + 1

	print(prod_map)
	total = sum(prod_map.values())
	print(f"Witnesses without producer info: {info_without_proc}. In total: {total}.")


def count_c_files():
	without_file = 0
	not_c_files_cnt = 0
	c_files_cnt = 0
	with os.scandir(WITNESS_INFO_BY_WITNESS_HASH_DIR) as it:
		for entry in it:
			if entry.name.startswith('.') or not entry.is_file():
				print(f"{entry.name} is not a file.")
				continue
			with open(entry.path) as f:
				jObj = json.load(f)
				if 'programfile' in jObj:
					programfile = str(jObj['programfile'])
					if programfile.endswith(".c"):
						c_files_cnt = c_files_cnt + 1
					else:
						not_c_files_cnt = not_c_files_cnt + 1
				else:
					without_file = without_file + 1

	print(
		f"Witnesses without program file info: {without_file}.\nIn total C files used: {c_files_cnt}, CIL files used: {not_c_files_cnt}")


def reach_c_files_count_unique_files():
	info_without_pf = 0
	not_sv_bench = 0
	file_map = {}
	with os.scandir(WITNESS_INFO_BY_WITNESS_HASH_DIR) as it:
		for entry in it:
			if entry.name.startswith('.') or not entry.is_file():
				print(f"{entry.name} is not a file.")
				continue
			with open(entry.path) as f:
				jObj = json.load(f)
				if 'programfile' in jObj and 'programhash':
					if not str(jObj['programfile']).endswith(".c") \
							or not str(
						jObj['specification']) == "CHECK( init(main()), LTL(G ! call(__VERIFIER_error())) )":
						continue

					filename = str(jObj['programhash'])
					if str(jObj['programfile']).find("sv-benchmarks/c") == -1:
						not_sv_bench = not_sv_bench + 1
						continue
					if filename in file_map:
						file_map[filename] = file_map[filename] + 1
					else:
						file_map[filename] = 1
				else:
					info_without_pf = info_without_pf + 1

	# print(file_map)
	total = sum(file_map.values())
	unique = len(file_map.keys())
	print(f"Witnesses without producer info: {info_without_pf}. In total files: {total}. In total unique: {unique}")
	print(f"Witnesses for non-SV-Benchmark: {not_sv_bench}.")



def main():
	parser = argparse.ArgumentParser(description="")
	parser.add_argument("input", type=str, help="The directory with unzipped witnesses.")
	# parser.add_argument("-c", "--config", required=True, type=str, help="The verifier configuration file.")

	args = parser.parse_args()
	if not setup_dirs(args.input):
		return 1
	# filter_violation_witnesses()
	# remove_non_matching_info_files()
	# count_producers()
	# count_c_files()
	reach_c_files_count_unique_files()


if __name__ == "__main__":
	main()
