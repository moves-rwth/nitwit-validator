import argparse

import math
import matplotlib.pyplot as plt
from matplotlib.colors import ListedColormap

plt.rcParams.update({'font.size': 36})
import numpy as np
import pandas as pd
import seaborn as sns

from common.utils import *

sns.set()
sns.set(font_scale=2)
sns.set_palette("colorblind")
sns.set_context("paper")  # talk/paper
# FIGSIZE = (20, 11.25)
# FONTSIZE = 26
DPI = 300
FORMAT = 'pdf'
BBOX = 'tight'
LEGEND_LOC_RIGHT = 'center right'
# LEGEND_LOC_BOTTOM = 'center bottom'
LEGEND_BBOX_ANCHOR = (1.25, 0.5)
LEGEND_NCOL = 1

STATUSES = {
	'false(unreach-call)': 0,
	'timeout (false(unreach-call))': 0,
	'unknown': 1,
	'-': 1,  # TODO: should be 'not run'?
	'true': 2,
	'timeout (true)': 2,
	'timeout': 3,
	'timeout (error (7))': 4,
	'exception': 4,
	'error (1)': 4,
	'error (7)': 4,
	'assertion': 4,
	'out of memory': 5,
	'error (2)': 6,
	'error (invalid witness file)': 6,
	0: 0,  # false
	245: 0,  # false, but not totally correct witness
	241: 1,  # error, witness in sink
	242: 1,  # program finished, witness not in violation state
	5: 1,  # error not reached
	250: 1,  # error not reached, witness in violation state
	9: 3,  # timeout
	4: 4,  # parse error
	-6: 4,  # picoc error
	6: 4,  # picoc error
	246: 4,  #
	251: 5,  # out of memory
	-11: 6,  # witness parse error
	11: 6,  # witness parse error
	2: 6,  # witness parse error
	244: 6,  # identifier undefined
}
col_names = ['False', 'Unknown', 'True', 'Timeout', 'Error', 'Out of memory', 'Bad witness']  # , 'not run']

col_names_small = ['false', 'unknown', 'true', 'to', 'error', 'om',
                   'bad witness']  # , 'sink', 'not in violation state']

VALIDATORS = {
	0: "CPAChecker",
	1: "Ultimate Automizer",
	2: "CPA-witness2test",
	3: "FShell-witness2test",
	4: "CWValidator"
}
VALIDATORS_ABBR = {
	0: "CPAChecker",
	1: "Ult. Auto.",
	2: "CPA-w2t",
	3: "FShell-w2t",
	4: "CWValidator"
}
VALIDATORS_LIST = ["CPAChecker", "Ultimate Automizer", "CPA-witness2test", "FShell-witness2test", "CWValidator"]
VALIDATORS_LIST_ABBR = ["CPAChecker", "Ult. Auto.", "CPA-w2t", "FShell-w2t", "CWValidator"]

CPU_MULTIPLIER = 2.1 / 3.4

SAVE_FIGURES = False


def adjust_to_cpu(time: float) -> float:
	return time * CPU_MULTIPLIER


def get_matching(all_results: List, validators: dict, outputmatched: str = None) -> Dict[str, dict]:
	by_witness = validators.keys()
	# witness_keys = [hash['witnessSHA'].lower() for key in by_witness for hash in validators[key]['results']]
	witness_keys = set(by_witness)

	matched = list(filter(lambda r: str(r[1]).partition('.json')[0] in witness_keys, all_results))
	matched_keys = list(map(lambda r: str(r[1]).partition('.json')[0], matched))
	if outputmatched is not None:
		with open(outputmatched, 'w') as fp:
			json.dump(matched, fp)
	for w in matched:
		wit_key = w[1].partition('.json')[0]
		validators[wit_key]['results'] \
			.insert(4, dict(
			{'cpu': adjust_to_cpu(w[3]), 'mem': int(w[5] if len(w) > 5 else 0) / 1000, 'tool': VALIDATORS_LIST[4],
			 'status': w[0]}))  # memory to MB, cpu in secs
		validators[wit_key]['creator'] = w[4]
	print(f"I could match {len(matched)} out of {len(all_results)} witnesses")
	return {k: v for k, v in validators.items() if k in matched_keys}


def analyze_output_messages(matching: Dict[str, dict]):
	print(f"Analyze {len(matching)}")
	data = np.zeros((7, 5), dtype=int)
	for i in range(5):
		for w, c in matching.items():
			data[STATUSES[c['results'][i]['status']], i] += 1

	df = pd.DataFrame(columns=VALIDATORS_LIST_ABBR, data=data, index=col_names)
	ax = df.T.sort_values(by=col_names[0]).plot(kind='bar', stacked=True,
	                                            # colormap=ListedColormap(sns.color_palette("colorblind")),
	                                            # figsize=FIGSIZE,
	                                            rot=0)
	ax.legend(loc='lower right')

	if SAVE_FIGURES:
		ax.get_figure().savefig(f'/home/jan/Documents/thesis/doc/thesis/res/imgs/output_msgs.{FORMAT}', dpi=DPI,
		                        bbox_inches=BBOX)


def join_val_non_val(validated: dict, nonvalidated: dict) -> dict:
	result = {}
	for k in list(validated.keys()) + list(nonvalidated.keys()):
		val, nval = 0, 0
		if k in nonvalidated:
			nval = nonvalidated[k]
		if k in validated:
			val = validated[k]
		result[k] = (val, nval)
	# sorted_vals = sorted(list(result.keys()))
	# return sorted_vals, [[result[k][0], result[k][1]] for k in sorted_vals]
	return result


def analyze_by_producer(matching: Dict[str, dict]):
	print(f"Analyze by producer {len(matching)} witnesses")
	mux = pd.MultiIndex.from_product([VALIDATORS_LIST_ABBR, ['val', 'nval']])
	df = pd.DataFrame(columns=mux)
	data = []
	for i in range(5):
		validated = {}
		nonvalidated = {}
		for w, c in matching.items():
			if STATUSES[c['results'][i]['status']] == 0:
				increase_count_in_dict(validated, c['creator'])
			else:
				increase_count_in_dict(nonvalidated, c['creator'])
		data.append(join_val_non_val(validated, nonvalidated))
	rows = set(np.asarray(list(map(lambda x: list(x.keys()), data))).flatten())
	for producer in rows:
		df.loc[producer] = [0] * len(VALIDATORS_LIST_ABBR) * 2
	for i, d in enumerate(data):
		for k, v in d.items():
			df.at[k, (VALIDATORS_LIST_ABBR[i], 'val')] = v[0]
			df.at[k, (VALIDATORS_LIST_ABBR[i], 'nval')] = v[1]
	df = df.sort_index()
	df.loc["Total"] = df.sum().astype(int)
	df['Total'] = df[VALIDATORS_ABBR[4]].sum(axis=1).astype(int)

	print(df.to_latex())


def validator_result_selector(results: list, predicate_others, predicate_cwv) -> bool:
	for i in range(4):
		if not predicate_others(STATUSES[results[i]['status']]):
			return False
	if predicate_cwv(STATUSES[results[4]['status']]):
		return True


def analyze_unique_by_producer(matching: Dict[str, dict], diff_matching: Dict[str, dict] = None) -> Tuple[
	set, set, set, set]:
	print(f"Analyze unique results by producer for {len(matching)} witnesses")
	others_uval = set()
	cwv_uval = set()
	none_val = set()
	all_val = set()
	for w, c in matching.items():
		if validator_result_selector(c['results'], lambda x: x == 0, lambda x: x != 0):
			others_uval.add(w + '.json')
		if validator_result_selector(c['results'], lambda x: x != 0, lambda x: x == 0):
			cwv_uval.add(w + '.json')
		if validator_result_selector(c['results'], lambda x: x == 0, lambda x: x == 0):
			all_val.add(w + '.json')
		if validator_result_selector(c['results'], lambda x: x != 0, lambda x: x != 0):
			none_val.add(w + '.json')

	print(f"Uniquely validated by *others*, i.e., CWV probably buggy: {len(others_uval)}")
	print(f"Uniquely validated by *CWValidator*: {len(cwv_uval)}")
	print(f"Validated by all, i.e. pretty sure these witnesses are correct: {len(all_val)}")
	print(f"Validated by none, i.e. pretty sure these witnesses are incorrect or too complex: {len(none_val)}\n")

	if diff_matching is not None:
		print(f"\nResults for diff:")
		diff_others, diff_cwv, diff_none, diff_all = analyze_unique_by_producer(diff_matching)
		print('-' * 40)
		print('Not in diff')
		print(f"Others: {others_uval.difference(diff_others)}")
		print(f"CWV: {cwv_uval.difference(diff_cwv)}")
		print(f"None: {none_val.difference(diff_none)}")
		print(f"All: {all_val.difference(diff_all)}")
		print()
		print('Not in original')
		print(f"Others: {diff_others.difference(others_uval)}")
		print(f"CWV: {diff_cwv.difference(cwv_uval)}")
		print(f"None: {diff_none.difference(none_val)}")
		print(f"All: {diff_all.difference(all_val)}")

	return others_uval, cwv_uval, none_val, all_val


def reject_outliers(data, m=2):
	return data[abs(data - np.mean(data)) < m * np.std(data)]


def analyze_times(matching: Dict[str, dict], name: str, inclusion_predicate):
	print('=' * 20 + ' CPU TIME (s) ' + name + '=' * 20)
	# fig, axs = plt.subplots(nrows=2, ncols=3)
	figsc, axsc = plt.subplots()
	# take just the successful ones
	for i in range(5):
		times = np.asarray(list(map(lambda x: float(x['results'][i]['cpu']),
		                            filter(lambda x: inclusion_predicate(STATUSES[x['results'][i]['status']]),
		                                   matching.values()))))
		# print(f"{VALIDATORS_ABBR[i]}: {get_stats(times)}")
		# ax = sns.distplot(reject_outliers(times), kde=False, rug=True, ax=axs[i % 2][math.floor(i / 2)], color="green")
		# ax.set_title(VALIDATORS_ABBR[i])
		# ax.set_xlabel("Time [s]")
		sns.lineplot(ax=axsc, y=sorted(times), x=range(len(times)), label=VALIDATORS_ABBR[i])

	# fig.delaxes(axs[1, 2])  # The indexing is zero-based here
	# if SAVE_FIGURES:
	# 	fig.savefig(f'/home/jan/Documents/thesis/doc/thesis/res/imgs/histo_times.{FORMAT}', dpi=DPI, bbox_inches=BBOX)
	axsc.set_ylabel("Time [s]")
	axsc.set_xlabel(f"Number of {name} Witnesses")
	axsc.set(yscale='log')
	axsc.grid(True, which='both')
	# axsc.legend(loc=LEGEND_LOC_RIGHT, bbox_to_anchor=LEGEND_BBOX_ANCHOR, ncol=LEGEND_NCOL)
	axsc.axhline(90, color='black', lw=1, linestyle='--')
	# axsc.set_yticks([0, 20, 40, 60, 80, 90, 100])

	if SAVE_FIGURES:
		figsc.savefig(f'/home/jan/Documents/thesis/doc/thesis/res/imgs/scatter_times_{name.lower()}.{FORMAT}', dpi=DPI,
		              bbox_inches=BBOX)

def get_shared_result(first: int, second: int):
	if first == second:
		return col_names[first]
	else:
		return 'Do not match'


def compare_times(matching: Dict[str, dict]):
	fig, axs = plt.subplots(nrows=2, ncols=2)
	cwv = list(map(lambda x: (float(x['results'][4]['cpu']), int(STATUSES[x['results'][4]['status']])),
	                            matching.values()))
	for i in range(4):
		times = list(map(lambda x: (float(x['results'][i]['cpu']), int(STATUSES[x['results'][i]['status']])),
		                                   matching.values()))
		filter_false = filter(lambda tup: tup[0][1] == tup[1][1] and (tup[0][1] == 0 or tup[0][1] == 3), zip(cwv, times))
		data = list(map(lambda tup: [tup[0][0], tup[1][0], get_shared_result(tup[0][1], tup[1][1])], filter_false))
		df = pd.DataFrame(data, columns=['x', 'y', 'Result'])
		ax = sns.scatterplot(x='x', y='y', hue='Result', data=df, ax=axs[i % 2][math.floor(i / 2)])
		ax.plot(ax.get_xlim(), ax.get_ylim(), ls="--", c=".3")
		ax.set_xlabel(f"{VALIDATORS_ABBR[4]} CPU Time [s]")
		ax.set_ylabel(f"{VALIDATORS_ABBR[i]} CPU Time [s]")

	if SAVE_FIGURES:
		fig.savefig(f'/home/jan/Documents/thesis/doc/thesis/res/imgs/quantile_times.{FORMAT}', dpi=DPI, bbox_inches=BBOX)


def compare_times_hex(matching: Dict[str, dict]):
	# fig, axs = plt.subplots(nrows=2, ncols=2)
	cwv = list(map(lambda x: (float(x['results'][4]['cpu']), int(STATUSES[x['results'][4]['status']])),
	                            matching.values()))
	for i in range(4):
		times = list(map(lambda x: (float(x['results'][i]['cpu']), int(STATUSES[x['results'][i]['status']])),
		                                   matching.values()))
		filter_false = filter(lambda tup: tup[0][1] == tup[1][1] and (tup[0][1] == 0 or tup[0][1] == 3), zip(cwv, times))
		data = list(map(lambda tup: [tup[0][0], tup[1][0], get_shared_result(tup[0][1], tup[1][1])], filter_false))
		df = pd.DataFrame(data, columns=['x', 'y', 'Result'])
		ax = sns.jointplot(x='x', y='y', data=df, kind='hex', ) #, ax=axs[i % 2][math.floor(i / 2)])
		ax.set_axis_labels(f"{VALIDATORS_ABBR[4]} CPU Time [s]", f"{VALIDATORS_ABBR[i]} CPU Time [s]")
		# ax.plot(ax.get_xlim(), ax.get_ylim(), ls="--", c=".3")

	# if SAVE_FIGURES:
	# 	fig.savefig(f'/home/jan/Documents/thesis/doc/thesis/res/imgs/quantile_times.{FORMAT}', dpi=DPI, bbox_inches=BBOX)


def analyze_memory(matching: Dict[str, dict], name: str, inclusion_predicate):
	print('=' * 20 + ' MEMORY (MB) ' + name + '=' * 20)
	figsc, axsc = plt.subplots()
	# take just the successful ones
	for i in range(5):
		mems = np.asarray(list(map(lambda x: float(x['results'][i]['mem']),
		                           filter(lambda x: inclusion_predicate(STATUSES[x['results'][i]['status']]),
		                                  matching.values()))))
		# print(f"{VALIDATORS_ABBR[i]}: {get_stats(mems)}")
		sns.lineplot(ax=axsc, y=sorted(mems), x=range(len(mems)), label=VALIDATORS_ABBR[i])

	axsc.set_ylabel("Memory [MB]")
	axsc.set_xlabel(f"Number of {name} Witnesses")
	axsc.set(yscale='log')
	axsc.grid(True, which='both')
	# axsc.legend(loc='upper left', bbox_to_anchor=(0.02, 0.9))
	axsc.axhline(7000, color='black', lw=1, linestyle='--')
	axsc.set_yticks([10, 100, 1000, 7000])
	if SAVE_FIGURES:
		figsc.savefig(f'/home/jan/Documents/thesis/doc/thesis/res/imgs/scatter_memory_{name.lower()}.{FORMAT}', dpi=DPI,
		              bbox_inches=BBOX)


def get_stats(times) -> str:
	return f"Mean: {np.mean(times)} Median: {np.median(times)} Std dev: {np.std(times)} In total: {np.sum(times)}"


def output_val_data(matching: Dict[str, dict]):
	data = []
	for w in matching.values():
		# for i in range(len(VALIDATORS_LIST_ABBR)):
		data.append(list(map(lambda i: STATUSES[w['results'][i]['status']], range(len(VALIDATORS_LIST_ABBR)))))

	df = pd.DataFrame(columns=VALIDATORS_LIST_ABBR, data=data, dtype=int)
	print(f"Validated at least once: {len(df[(df == 0).sum(1) > 0])}"
	      f"  Once: {len(df[(df == 0).sum(1) == 1])},"
	      f"  Twice: {len(df[(df == 0).sum(1) == 2])},"
	      f"  Thrice: {len(df[(df == 0).sum(1) == 3])},"
	      f"  4 times: {len(df[(df == 0).sum(1) == 4])},"
	      f"  5 times: {len(df[(df == 0).sum(1) == 5])}")


def main():
	global SAVE_FIGURES
	parser = argparse.ArgumentParser(description="Analyzes results of CWValidator and SV-COMP validators")
	parser.add_argument("-v", "--validators", required=True, type=str,
	                    help="The JSON file with results about SV-COMP validator runs.")
	parser.add_argument("-r", "--results", required=True, type=str,
	                    help="The directory with validation results of CWValidator.")
	parser.add_argument("-om", "--outputmatched", required=False, type=str,
	                    help="File where to write the matched files config.")
	parser.add_argument("-df", "--diff", required=False, type=str,
	                    help="Directory with other results to compare.")
	parser.add_argument("-s", "--save", required=False, default=False, action='store_true',
	                    help="Save figures into thesis directory.")
	parser.add_argument("-g", "--graph", required=False, default=False, action='store_true', help="Show graphs.")
	args = parser.parse_args()
	SAVE_FIGURES = args.save

	results = load_result_files(args.results)
	val, nval, bpar = results
	all = val + nval + bpar

	validators = load_validators_result_file(args.validators)
	if results is None or validators is None:
		return 1

	matching = get_matching(all, validators['byWitnessHash'], args.outputmatched)

	if args.outputmatched:
		output_val_data(matching)

	######### ANALYSES ###########
	# analyze_output_messages(matching)
	# analyze_by_producer(matching)
	# analyze_unique_by_producer(matching, diff_matching)
	# for i in (0, 1, 2):
	# 	analyze_times(matching, col_names[i], lambda x: x == i)
	# 	analyze_memory(matching, col_names[i], lambda x: x == i)
	# analyze_times(matching, 'Other', lambda x: x > 2)
	# analyze_memory(matching, 'Other', lambda x: x > 2)
	# analyze_times(matching, 'All', lambda x: True)
	# analyze_memory(matching, 'All', lambda x: True)
	compare_times_hex(matching)
	compare_times(matching)
	if args.graph:
		plt.show()


if __name__ == "__main__":
	main()
