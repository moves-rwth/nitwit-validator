import argparse

import math
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import seaborn as sns

from common.utils import *

sns.set()

STATUSES = {
	'false(unreach-call)': 0,
	'unknown': 1,
	'true': 2,
	'timeout': 3,
	'timeout (true)': 3,
	'timeout (error (7))': 3,
	'timeout (false(unreach-call))': 3,
	'exception': 4,
	'error (1)': 4,
	'error (2)': 4,
	'error (7)': 4,
	'error (invalid witness file)': 4,
	'assertion': 4,
	'out of memory': 5,
	'-': 6
}
col_names = ['false', 'unknown', 'true', 't/o', 'error', 'o/m', 'not run']

RESULT_CODES = {
	0: 0,  # false
	245: 0,  # false, but not totally correct witness
	4: 1,  # parse error
	246: 1,  #
	244: 1,  # identifier undefined
	5: 2,  # error not reached
	250: 2,  # error not reached, witness in violation state
	9: 3,  # timeout
	241: 4,  # error, witness in sink
	242: 5,  # program finished, witness not in violation state
}
row_names = ['false', 'unknown', 'not reached', 't/o', 'sink', 'not in violation state']

VALIDATORS = {
	0: "CPAChecker",
	1: "Ultimate Automizer",
	2: "CPA-witness2test",
	3: "CProver"
}


def get_matching(all_results: List, validators: dict, outputmatched: str = None) -> Dict[str, tuple]:
	by_witness = validators.keys()
	# witness_keys = [hash['witnessSHA'].lower() for key in by_witness for hash in validators[key]['results']]
	witness_keys = set(by_witness)

	matched = list(filter(lambda r: str(r[1]).partition('.json')[0] in witness_keys, all_results))
	if outputmatched is not None:
		with open(outputmatched, 'w') as fp:
			json.dump(matched, fp)
	print(f"I could match {len(matched)} out of {len(all_results)} witnesses")
	return dict([(r[1].partition('.json')[0], (r[0], r[3], validators[r[1].partition('.json')[0]])) for r in matched])


def analyze(matching: Dict[str, tuple], validators: dict):
	print(f"Analyze {len(matching)}")
	fig, axs = plt.subplots(nrows=2, ncols=2)
	for i in range(4):
		name = None
		heatmap = np.zeros((6, 7), dtype=int)
		for w, c in matching.items():
			if name is None:
				name = c[2]['results'][i]['tool']
			heatmap[RESULT_CODES[c[0]], STATUSES[c[2]['results'][i]['status']]] += 1
		dataframe = pd.DataFrame(data=heatmap, index=row_names, columns=col_names)
		ax = sns.heatmap(dataframe, annot=True, fmt="d", ax=axs[math.floor(i / 2)][i % 2], cmap="BuGn")
		ax.set_title(name)

	# plt.show()


def join_val_non_val(validated: dict, nonvalidated: dict) -> dict:
	result = {}
	for k in list(validated.keys()) + list(nonvalidated.keys()):
		val, nval = 0, 0
		if k in nonvalidated:
			nval = nonvalidated[k]
		if k in validated:
			val = validated[k]
		result[k] = {'val': val, 'nval': nval}
	return result


def analyze_by_producer(matching: Dict[str, tuple], validators: dict):
	print(f"Analyze by producer {len(matching)} witnesses")
	for i in range(4):
		validated = {}
		nonvalidated = {}
		name = None
		for w, c in matching.items():
			if name is None:
				name = validators[w]['results'][i]['tool']
			if STATUSES[validators[w]['results'][i]['status']] == 0:
				increase_count_in_dict(validated, validators[w]['creator'])
			else:
				increase_count_in_dict(nonvalidated, validators[w]['creator'])
		print(f"Validator {name}:")
		print(join_val_non_val(validated, nonvalidated))
		print("-" * 40)
	validated = {}
	nonvalidated = {}
	for w, c in matching.items():
		if RESULT_CODES[c[0]] == 0:
			increase_count_in_dict(validated, validators[w]['creator'])
		else:
			increase_count_in_dict(nonvalidated, validators[w]['creator'])
	print("CWValidator:")
	print(join_val_non_val(validated, nonvalidated))
	print("-" * 40)


def reject_outliers(data, m=2):
	return data[abs(data - np.mean(data)) < m * np.std(data)]


def analyze_times(matching: Dict[str, tuple], validators: dict):
	fig, axs = plt.subplots(nrows=2, ncols=3)
	# take just the successful ones
	for i in range(4):
		times = np.asarray(list(map(lambda x: float(x[2]['results'][i]['cpu']), filter(lambda x: STATUSES[x[2]['results'][i]['status']] == 0, matching.values()))))
		print(f"Validator {VALIDATORS[i]}:")
		print_stats(times)
		print("-" * 40)
		ax = sns.distplot(reject_outliers(times), kde=True, rug=True, ax=axs[math.floor(i / 2)][i % 2], color="green")
		ax.set_title(VALIDATORS[i])

	times = np.asarray(list(map(lambda x: x[1], filter(lambda x: RESULT_CODES[x[0]] == 0, matching.values()))))
	print("CWValidator:")
	print_stats(times)
	print("-" * 40)
	ax = sns.distplot(reject_outliers(times), kde=True, rug=True, ax=axs[0][2], color="green")
	ax.set_title("CWValidator")
	fig.delaxes(axs[1, 2])  # The indexing is zero-based here


def print_stats(times):
	print(f"Mean: {np.mean(times)}")
	print(f"Median: {np.median(times)}")
	print(f"Std dev: {np.std(times)}")


def main():
	parser = argparse.ArgumentParser(description="Analyzes results of CWValidator and SV-COMP validators")
	parser.add_argument("-v", "--validators", required=True, type=str,
	                    help="The JSON file with results about SV-COMP validator runs.")
	parser.add_argument("-r", "--results", required=True, type=str,
	                    help="The directory with validation results of CWValidator.")
	parser.add_argument("-om", "--outputmatched", required=False, type=str,
	                    help="File where to write the matched files config.")

	args = parser.parse_args()

	results = load_result_files(args.results)
	val, nval, bpar = results
	all = val + nval + bpar

	validators = load_validators_result_file(args.validators)
	if results is None or validators is None:
		return 1

	matching = get_matching(all, validators['byWitnessHash'], args.outputmatched)
	analyze(matching, validators['byWitnessHash'])

	# analyze_by_producer(matching, validators['byWitnessHash'])

	analyze_times(matching, validators['byWitnessHash'])
	plt.show()


if __name__ == "__main__":
	main()
