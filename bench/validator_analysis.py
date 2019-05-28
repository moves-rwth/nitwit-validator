import argparse
import os
import sys
import json
from typing import Tuple, List, Optional, Dict

from common.utils import *


def analyze(results: Tuple[List, ...], validators: dict):
	val, nval, bpar = results
	by_witness = validators['byWitnessHash'].keys()
	witness_keys = set([hash['witnessSHA'].lower() for key in by_witness for hash in validators['byWitnessHash'][key]['results']])

	found = 0
	all = val + nval + bpar
	for r in all:
		if str(r[1]).partition('.json')[0].lower() in witness_keys:
			found = found + 1
		# else:
		# 	print(r)
		# 	break
	print(f"I could match {found} out of {len(all)} witnesses")


def main():
	parser = argparse.ArgumentParser(description="Analyzes results of CWValidator and SV-COMP validators")
	parser.add_argument("-v", "--validators", required=True, type=str,
	                    help="The JSON file with results about SV-COMP validator runs.")
	parser.add_argument("-r", "--results", required=True, type=str,
	                    help="The directory with validation results of CWValidator.")

	args = parser.parse_args()

	results = load_result_files(args.results)
	validators = load_validators_result_file(args.validators)
	if results is None or validators is None:
		return 1

	analyze(results, validators)


if __name__ == "__main__":
	main()
