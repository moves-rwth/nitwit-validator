import argparse
import os
import re
import sys
import json
from typing import Tuple, List, Optional, Dict

from common.utils import *


def results_filter(results: list, code: int = None, regex: str = None) -> list:
	return list(filter(lambda x: (code is None or x[0] == code) and (regex is None or re.match(regex, x[2]) is not None), results))


def main():
	parser = argparse.ArgumentParser(description="Filters results by a specified code and regex.")
	parser.add_argument("-r", "--results", required=True, type=str, help="A file with validation results of CWValidator.")
	parser.add_argument("-c", "--code", required=False, type=int, help="Output code for the result.")
	parser.add_argument("-m", "--match", required=False, type=str, help="Regex for matching the error output.")

	args = parser.parse_args()
	results = load_result_file(args.results)
	filtered = results_filter(results, args.code, args.match)
	# print(f"Found {len(filtered)} results.")
	print(json.dumps(filtered))


if __name__ == "__main__":
	main()
