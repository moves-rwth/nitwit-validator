import argparse
import os
import re
import sys
import json
from typing import Tuple, List, Optional, Dict

from common.utils import *


def get_file_name(witnesshash: str):
	with open(os.path.join('/home/jan/Documents/thesis/data/sv-witnesses/witnessInfoByHash', witnesshash), 'r') as fp:
		obj = json.load(fp)
	return obj['programfile']


def results_filter(results: list, code: int = None, regex: str = None, producer: str = None,
                   property: str = None, exproperty: str = None) -> list:
	return list(filter(lambda x: (code is None or x[0] == code) and
	                             (regex is None or re.match(regex, x[2]) is not None) and
	                             (property is None or re.match(property, get_file_name(x[1])) is not None) and
	                             (property is None or re.match(property, get_file_name(x[1])) is not None) and
	                             (exproperty is None or re.match(exproperty, get_file_name(x[1])) is None) and
	                             (producer is None or re.match(producer, x[4]) is not None),
	                   results))


def main():
	parser = argparse.ArgumentParser(description="Filters results by a specified code and regex. Output to stdout.")
	parser.add_argument("-r", "--results", required=True, type=str,
	                    help="A file with validation results of Nitwit.")
	parser.add_argument("-c", "--code", required=False, type=int, help="Output code for the result.")
	parser.add_argument("-m", "--match", required=False, type=str, help="Regex for matching the error output.")
	parser.add_argument("-p", "--producer", required=False, type=str, help="Regex for matching the producer.")
	parser.add_argument("-prp", "--property", required=False, type=str,
	                    help="Regex for matching the property in program file.")
	parser.add_argument("-exprp", "--excludeproperty", required=False, type=str,
	                    help="Regex for not-matching the property in program file.")

	args = parser.parse_args()
	f, nv, bv = load_result_files(args.results)
	results = f + nv + bv
	filtered = results_filter(results, args.code, args.match, args.producer, args.property, args.excludeproperty)
	print(json.dumps(filtered))


if __name__ == "__main__":
	main()
