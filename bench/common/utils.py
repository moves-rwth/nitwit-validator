import datetime
import json
import os
import re
from typing import Tuple, List, Dict, Optional


def output(output_path: str,
           validated: List[Tuple[int, str, str, float, str, str, int]],
           non_validated: List[Tuple[int, str, str, float, str, str, int]],
           badly_parsed: List[Tuple[int, str, str, float, str, str, int]]):
	if not os.path.exists(output_path):
		os.makedirs(output_path)

	with open(os.path.join(output_path, f"validated_witnesses.json"), "w") as val_fp:
		json.dump(validated, val_fp)

	with open(os.path.join(output_path, f"non_validated_witnesses.json"), "w") as non_val_fp:
		json.dump(non_validated, non_val_fp)

	with open(os.path.join(output_path, f"badly_parsed_witnesses.json"), "w") as badly_parsed_fp:
		json.dump(badly_parsed, badly_parsed_fp)

def save_table_to_file(table_data: str, name: str, TABLE_DIR: str):
	tex_file = r'''\documentclass[notitlepage]{article}
			\usepackage{booktabs}
			\usepackage{lscape}
			\usepackage[scale=0.75,top=3cm]{geometry}		
			\begin{document}''' + table_data + '\end{document}'

	path = f'{TABLE_DIR}/table_{name.lower()}.tex'
	with open(path,'w') as f:
	    f.write(tex_file)
	
	# generate pdflatex and redirect output too make console output readable
	os.system(f"pdflatex -output-directory='{TABLE_DIR}' {TABLE_DIR}/table_{name.lower()}.tex > /dev/null")
	
	# delete generated log and aux file
	os.unlink(f"{TABLE_DIR}/table_{name.lower()}.log")
	os.unlink(f"{TABLE_DIR}/table_{name.lower()}.aux")

def parse_message(errmsg, out, process):
	if process.returncode != 0 and out is not None:
		outs = str(out)
		pos = outs.find(' ### ')
		if pos != -1:
			endpos = outs.find('\\n', pos)
			if endpos == -1:
				endpos = len(outs) - 1
			errmsg = outs[(pos + 5):endpos]
		else:
			pos = outs.rfind(' #*# ')
			if pos != -1:
				endpos = outs.find('\\n', pos)
				if endpos == -1:
					endpos = len(outs) - 1
				errmsg = outs[(pos + 5):endpos]
	else:			
		errmsg = 'Msg not parsed'
	return errmsg

def parse_stderr_message(stderr_list, err, process):
	if err is not None:
		errs = str(err)
		if re.search(r' ### ', errs) is not None:			
			stderr_msg = ''			
			iterator = re.finditer(r' ### ', errs)
			for index in iterator:
				startpos = int(index.start())
				endpos = errs.find('\\n', startpos)
				if endpos == -1:
					endpos = len(errs) - 1
				stderr_msg = errs[(startpos + 5):endpos]
				stderr_list.append(stderr_msg)
		else:			
			stderr_list = ['No msg found']
	else:
		stder_list = ["stderr was empty!"]
	return stderr_list


def process_results(results: List[Tuple[int, str, str, List, float, str, str, int]], header: Tuple[str, str, str, str, str, str, str, str], executable: str, out: bool):
	validated = [header]
	non_validated = [header]
	badly_parsed = [header]
	for ret_code, info_file, out_msg, err_msg, time, prod, source, mem in results:
		result_record = (abs(ret_code), os.path.basename(info_file), out_msg, err_msg, time, prod, source, mem)
		if ret_code is None or ret_code == -9:
			non_validated.append(result_record)
		elif ret_code == 0 or ret_code == 245:
			validated.append(result_record)
		elif 240 <= ret_code <= 243 or ret_code == 250:
			non_validated.append(result_record)
		elif ret_code == 244 or ret_code == 246:
			badly_parsed.append(result_record)
		elif ret_code == 2:
			badly_parsed.append(result_record)
		elif ret_code == 3:
			print(f"Bad usage: {info_file}")
			badly_parsed.append(result_record)
		elif ret_code == 4 or ret_code == 5:
			non_validated.append(result_record)
		else:
			# print(f"Other error: {ret_code}, {info_file}")
			badly_parsed.append(result_record)

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
	if not (os.path.exists(results)):
		print("Cannot load output directory with info about witnesses.")
		return None
	if os.path.isdir(results):
		out = []
		file_names = ['validated_witnesses.json', 'non_validated_witnesses.json', 'badly_parsed_witnesses.json']
		for i, fn in enumerate(file_names):
			with open(os.path.join(results, fn), 'r') as fp:
				jObj = json.load(fp)
			out.append(list(jObj))
		return tuple(out)
	elif os.path.isfile(results):
		with open(results, 'r') as fp:
			jObj = json.load(fp)
		return list(jObj), [], []
	else:
		raise Exception("What to do?")


def load_result_file(results: str) -> Optional[list]:
	if not (os.path.exists(results) and os.path.isfile(results)):
		print("Cannot load output file with info about validation results.")
		return None
	with open(results, 'r') as fp:
		valid_jObj = json.load(fp)
	return list(valid_jObj)


def load_validators_result_file(validators: str) -> Optional[dict]:
	if not (os.path.exists(validators) and os.path.isfile(validators)):
		print("Cannot load output file with info about validator benchmark from SV-COMP.")
		return None

	with open(validators, 'r') as fp:
		return json.load(fp)
