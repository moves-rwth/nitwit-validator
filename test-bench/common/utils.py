import datetime
import json
import os
from typing import Tuple, List


def output(output_path: str, validated: List[str], non_validated: List[str], badly_parsed: List[str]):
	if not os.path.exists(output_path):
		os.makedirs(output_path)

	time = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")

	with open(os.path.join(output_path, f"validated_witnesses_{time}"), "w") as val_fp:
		json.dump(validated, val_fp)

	with open(os.path.join(output_path, f"non_validated_witnesses_{time}"), "w") as non_val_fp:
		json.dump(non_validated, non_val_fp)

	with open(os.path.join(output_path, f"badly_parsed_witnesses_{time}"), "w") as badly_parsed_fp:
		json.dump(badly_parsed, badly_parsed_fp)


def process_results(results: List[Tuple[int, str]], out: bool):
	validated = []
	non_validated = []
	badly_parsed = []
	for ret_code, info_file in results:
		if ret_code == 0:
			validated.append(os.path.basename(info_file))
		elif ret_code == 1:
			non_validated.append(os.path.basename(info_file))
		elif ret_code == 2:
			print(f"Witness parse error: {info_file}")
		elif ret_code == 3:
			print(f"Bad usage: {info_file}")
		elif ret_code == 4:
			badly_parsed.append(os.path.basename(info_file))
		else:
			print(f"Other error: {ret_code}, {info_file}")

	if out:
		output(os.path.join("output", os.path.basename(VALIDATOR_EXECUTABLE)), validated, non_validated, badly_parsed)
	else:
		print(f"Validated: {validated}")
		print(f"Non-validated: {non_validated}")
		print(f"Badly parsed: {badly_parsed}")