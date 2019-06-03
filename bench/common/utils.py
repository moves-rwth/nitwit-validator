import datetime
import json
import os
from typing import Tuple, List, Dict, Optional


def output(output_path: str,
           validated: List[Tuple[int, str, str, float]],
           non_validated: List[Tuple[int, str, str, float]],
           badly_parsed: List[Tuple[int, str, str, float]]):
	if not os.path.exists(output_path):
		os.makedirs(output_path)

	with open(os.path.join(output_path, f"validated_witnesses.json"), "w") as val_fp:
		json.dump(validated, val_fp)

	with open(os.path.join(output_path, f"non_validated_witnesses.json"), "w") as non_val_fp:
		json.dump(non_validated, non_val_fp)

	with open(os.path.join(output_path, f"badly_parsed_witnesses.json"), "w") as badly_parsed_fp:
		json.dump(badly_parsed, badly_parsed_fp)


def process_results(results: List[Tuple[int, str, str, float]], executable: str, out: bool):
	validated = []
	non_validated = []
	badly_parsed = []
	for ret_code, info_file, err_msg, time in results:
		if ret_code is None:
			non_validated.append((9, os.path.basename(info_file), err_msg, time))
		elif ret_code == 0 or ret_code == 245:
			validated.append((ret_code, os.path.basename(info_file), err_msg, time))
		elif 240 <= ret_code <= 243 or ret_code == 250:
			non_validated.append((ret_code, os.path.basename(info_file), err_msg, time))
		elif ret_code == 244 or ret_code == 246:
			badly_parsed.append((ret_code, os.path.basename(info_file), err_msg, time))
		elif ret_code == 2:
			print(f"Witness parse error: {info_file}")
		elif ret_code == 3:
			print(f"Bad usage: {info_file}")
		elif ret_code == 4 or ret_code == 5:
			non_validated.append((ret_code, os.path.basename(info_file), err_msg, time))
		else:
			print(f"Other error: {ret_code}, {info_file}")

	if out:
		time = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
		output(os.path.join("output", f"{os.path.basename(executable)}_{time}"), validated, non_validated, badly_parsed)
	else:
		print(f"Validated: {validated}")
		print(f"Non-validated: {non_validated}")
		print(f"Badly parsed: {badly_parsed}")


def increase_count_in_dict(m: Dict[any, int], el: any):
	if el in m:
		m[el] = m[el] + 1
	elif el is not None:
		m[el] = 1


def load_result_files(results: str) -> Optional[Tuple[list, ...]]:
	if not (os.path.exists(results) and os.path.isdir(results)):
		print("Cannot load output directory with info about witnesses.")
		return None
	out = []
	file_names = ['validated_witnesses.json', 'non_validated_witnesses.json', 'badly_parsed_witnesses.json']
	for i, fn in enumerate(file_names):
		with open(os.path.join(results, fn), 'r') as fp:
			valid_jObj = json.load(fp)
		out.append(list(valid_jObj))
	return tuple(out)


def load_result_file(results: str) -> Optional[list]:
	if not (os.path.exists(results) and os.path.isfile(results)):
		print("Cannot load output file with info about validation results.")
		return None
	with open(results, 'r') as fp:
		valid_jObj = json.load(fp)
	return list(valid_jObj)


def load_validators_result_file(validators: str) -> Optional[dict]:
	if not (os.path.exists(validators) and os.path.isfile(validators)):
		print("Cannot load output file with info about validators.")
		return None

	with open(validators, 'r') as fp:
		return json.load(fp)


