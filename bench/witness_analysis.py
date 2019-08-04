import argparse
import json
import os
from typing import List, Tuple
import pandas as pd

COLUMNS = ['Tool', 'nodes', 'edges', 'assumptions', 'controls', 'enters', 'returns']


def run(entry: Tuple[str, str]):
	with open(entry[0]) as f:
		witness_textual = f.read()
		return (entry[1], witness_textual.count('<node'), witness_textual.count('<edge'),
		        witness_textual.count('key=\"assumption\"'), witness_textual.count('key=\"control\"'),
		        witness_textual.count('key=\"enterFunction\"'), witness_textual.count('key=\"returnFrom\"'))


def run_analysis_parallel(configs: List[Tuple[str, str, str, str]]):
	witnesses = [(entry[0], entry[3]) for entry in configs]
	results = map(run, witnesses)
	return list(results)


def get_bench_configs(path_to_configs: str) -> List[Tuple[str, str, str, str]]:
	if not os.path.exists(path_to_configs) or not os.path.isfile(path_to_configs):
		print(f"Could not read from {path_to_configs}. Does the file exist?")
		raise
	with open(path_to_configs) as fp:
		configs = json.load(fp)
		return configs


def main():
	parser = argparse.ArgumentParser(description="Runs the CWValidator on SV-Benchmark")
	parser.add_argument("-c", "--config", required=True, type=str, help="The executions configuration file.")
	parser.add_argument("-l", "--limit", required=False, type=int, default=None, help="How many witnesses to process.")

	args = parser.parse_args()
	configs = get_bench_configs(args.config)
	import random
	random.shuffle(configs)
	if args.limit is not None:
		configs = configs[:args.limit]

	results = run_analysis_parallel(configs)

	df = pd.DataFrame(data=results, columns=COLUMNS)
	df_max = df.copy()
	# df_sum = df.copy()
	# df_min = df.copy()
	# df_sum = df_sum.groupby(COLUMNS[0]).sum()
	# df_min = df_min.groupby(COLUMNS[0]).min()
	# print(df_sum. to_latex(float_format='%.1f'))
	# print(df_min. to_latex(float_format='%.1f'))


	df_mean = df.groupby(COLUMNS[0]).mean()
	df_mean.loc["Overall"] = df.mean()
	df_max = df_max.groupby(COLUMNS[0]).max()
	df_max.loc["Overall"] = df.max()

	print(df_mean.to_latex(float_format='%.1f'))
	print(df_max. to_latex(float_format='%.1f'))



if __name__ == "__main__":
	main()
