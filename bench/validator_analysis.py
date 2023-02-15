import argparse

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import seaborn as sns

from common.utils import *

sns.set()
sns.set(font_scale=2)
sns.set_palette("colorblind")
sns.set_context("paper")  # choose "talk" or "paper"
sns.set_style('whitegrid')
DPI = 300
FIGURE_FORMAT = 'pdf'
TABLE_FORMAT = 'tex'
BBOX = 'tight'
LEGEND_LOC_RIGHT = 'center right'
LEGEND_BBOX_ANCHOR = (1.25, 0.5)
LEGEND_NCOL = 1
plt.rcParams.update({'font.size': 36})

STATUSES = {
    'false(unreach-call)': 0,
    'FALSE(unreach-call)': 0,

    'unknown': 1,
    'UNKNOWN': 1,
    '-': 1,  # TODO: should be 'not run'?

    'true': 2,
    'TRUE': 2,

    'timeout (false(unreach-call))': 3,
    'TIMEOUT (false(unreach-call))': 3,
    'timeout (true)': 3,
    'TIMEOUT (true)': 3,
    'TIMEOUT (ASSERTION)': 3,
    'TIMEOUT (EXCEPTION)': 3,
    'timeout': 3,
    'TIMEOUT': 3,
    'TIMEOUT (ERROR (1))': 3,
    'TIMEOUT (ERROR (7))': 3,
    'TIMEOUT (OUT OF JAVA MEMORY)': 3,
    'timeout (error (7))': 3,
    'TIMEOUT (error (7))': 3,
    'TIMEOUT (verification)': 3,
    'TIMEOUT (ERROR (parsing failed))': 3,
    'TIMEOUT (before-instr)': 3,
    'TIMEOUT (KILLED (signal 9, verification))': 3,

    'exception': 4,
    'EXCEPTION': 4,
    'ERROR (true)': 4,
    'ERROR': 4,
    'ERROR (recursion) (true)': 4,
    'ERROR (false(unreach-call))': 4,
    'error (1)': 4,
    'error (7)': 4,
    'error (parsing failed)': 4,
    'ERROR (1)': 4,
    'ERROR (7)': 4,
    'ERROR (parsing failed)': 4,
    'ERROR (134)': 4,
    'ERROR (139)': 4,
    'ERROR (242)': 4,
    'ERROR (250)': 4,
    'ERROR (4)': 4,
    'ERROR(returned 1, verification-finished)': 4,
    'assertion': 4,  # TODO find out what exactly this means and if there should be a new status category for it
    'ASSERTION': 4,

    'out of memory': 5,
    'OUT OF MEMORY': 5,
    'OUT OF JAVA MEMORY': 5,
    'OUT OF MEMORY (KILLED (signal 9, verification))': 5,

    'error (2)': 4,
    # TODO 2 and 244 are codes nitwit outputs for bad-witness. But should it be rather in category 4 due to "ERROR"???
    'ERROR (2)': 4,
    'ERROR (244)': 4,
    'error (invalid witness file)': 4,
    'ERROR (invalid witness file)': 4,

    # -------------
    # following are based on exit codes from our benchmark script
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
    -11: 4,  # witness parse error
    11: 4,  # witness parse error
    8: 4,  # assertion failed
    2: 4,  # witness parse error
    244: 4,  # identifier undefined
}
col_names = ['False', 'Unknown', 'True', 'Timeout', 'Error', 'Out of memory']  # , 'not run']
col_names_small = ['false', 'unknown', 'true', 'to', 'error', 'om']  # , 'sink', 'not in violation state']

VALIDATORS = {}
VALIDATORS_ABBR = {}
VALIDATORS_FILES = {}
VALIDATORS_LIST_ABBR = []
NITWIT = "-1"
YEAR = -1
TABLE_DIR = ''
FIGURE_DIR = ''

COLUMN_INDEX = {
    'status': 0,
    'wit_key': 0,
    'out': 0,
    'err_out': 0,
    'cpu': 0,
    'tool': 0,
    'source': 0,
    'mem': 0
}

SAVE_FIGURES = False
SAVE_TABLES = False


def set_header_index(header: Tuple[str, str, str, str, str, str, str, str]):
    for i in range(len(header)):
        COLUMN_INDEX[header[i]] = i


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
        wit_key = w[COLUMN_INDEX['wit_key']].partition('.json')[0]
        validators[wit_key]['results'] \
            .insert(int(NITWIT), dict(
            {'cpu': w[COLUMN_INDEX['cpu']],
             'mem': int(w[COLUMN_INDEX['mem']] if len(w) > 5 else 0) / 1000,
             'tool': VALIDATORS_ABBR[NITWIT], 'status': w[COLUMN_INDEX['status']]}))  # memory to MB, cpu in secs
        validators[wit_key]['creator'] = w[COLUMN_INDEX['tool']]
    print(f"I could match {len(matched)} out of {len(all_results)} witnesses")
    return {k: v for k, v in validators.items() if k in matched_keys}


def get_status(results: dict, index: str) -> int:
    if index not in results:
        print(f"Error: Index {index} is not in results: {results}")
        exit(1)
    status = results[index]['status']
    if status not in STATUSES:
        print(f"Error: Could not find '{status}' in STATUSES!")
        exit(1)
    return STATUSES[status]


def analyze_output_messages(matching: Dict[str, dict]):
    print(f"Analyze {len(matching)}")
    data = np.zeros((len(col_names), len(VALIDATORS)), dtype=int)

    max_a = -1
    for i, val_index in enumerate(VALIDATORS.keys()):
        for w, c in matching.items():
            index_a = get_status(c['results'], val_index)
            index_b = int(i)
            max_a = max(index_a, max_a)
            # print(f"Accessing [{index_a}, {index_b}].")
            data[index_a, index_b] += 1

    df = pd.DataFrame(columns=VALIDATORS_LIST_ABBR, data=data, index=col_names)
    ax = df.T.sort_values(by=col_names[0]).plot(kind='bar', stacked=True, rot=0)
    ax.legend(loc='lower right')

    if SAVE_FIGURES:
        ax.get_figure().savefig(f'{FIGURE_DIR}/output_msgs.{FIGURE_FORMAT}', dpi=DPI,
                                bbox_inches=BBOX)
        plt.close(ax.get_figure())


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


def analyze_by_producer(val_results: Dict[str, dict]):
    print(f"Analyze by producer {len(val_results)} witnesses")
    mux = pd.MultiIndex.from_product([VALIDATORS_LIST_ABBR, ['val', 'nval']])

    df = pd.DataFrame(columns=mux)
    data = []
    for i, val_key in enumerate(VALIDATORS.keys()):
        validated = {}
        nonvalidated = {}
        for w, c in val_results.items():

            if get_status(c['results'], val_key) == 0:
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
    df['Total'] = df[VALIDATORS_ABBR[NITWIT]].sum(axis=1).astype(int)

    if SAVE_TABLES:
        save_table_to_file(df.to_latex(), 'analyse_by_producer', TABLE_DIR)


def analyze_virt_best(matching: Dict[str, dict]):
    df = pd.DataFrame(columns=VALIDATORS_LIST_ABBR + ['Virtual best'])
    data = []
    producers_virt = {}
    for i, val_key in enumerate(VALIDATORS.keys()):
        validated = {}
        for w, c in matching.items():
            status = get_status(c['results'], val_key)
            if status == 0:
                if c['creator'] in producers_virt:
                    producers_virt[c['creator']].add(c['witnessSHA'])
                else:
                    producers_virt[c['creator']] = {c['witnessSHA']}

            if status == 0:
                increase_count_in_dict(validated, c['creator'])
        data.append(validated)
    prods = np.asarray(list(map(lambda x: list(x.keys()), data)))
    prods_set = set()
    for l in prods:
        for p in l:
            prods_set.add(p)

    for producer in prods_set:
        df.loc[producer] = [0] * (len(VALIDATORS_LIST_ABBR) + 1)
    for i, d in enumerate(data):
        for k, v in d.items():
            df.at[k, VALIDATORS_LIST_ABBR[i]] = v
    df = df.sort_index()

    for producer, val_set in producers_virt.items():
        df.at[producer, 'Virtual best'] = len(val_set)
    df.loc["Total"] = df.sum().astype(int)

    if SAVE_TABLES:
        save_table_to_file(r'\begin{landscape}' + df.to_latex() + r'\end{landscape}', 'analyse_virt_best', TABLE_DIR)


def validator_result_selector(results: dict, predicate_others, predicate_nitwit) -> bool:
    for val_key in VALIDATORS.keys() - NITWIT:
        if not predicate_others(get_status(results, val_key)):
            return False

    if predicate_nitwit(get_status(results, NITWIT)):
        return True


def analyze_unique_by_producer(matching: Dict[str, dict], diff_matching: Dict[str, dict] = None) -> Tuple[
    set, set, set, set]:
    print(f"Analyze unique results by producer for {len(matching)} witnesses")
    others_uval = set()
    nitwit_uval = set()
    none_val = set()
    all_val = set()
    for w, c in matching.items():
        if validator_result_selector(c['results'], lambda x: x == 0, lambda x: x != 0):
            others_uval.add(w + '.json')
        if validator_result_selector(c['results'], lambda x: x != 0, lambda x: x == 0):
            nitwit_uval.add(w + '.json')
        if validator_result_selector(c['results'], lambda x: x == 0, lambda x: x == 0):
            all_val.add(w + '.json')
        if validator_result_selector(c['results'], lambda x: x != 0, lambda x: x != 0):
            none_val.add(w + '.json')

    print(f"Uniquely validated by *others*, i.e., CWV probably buggy: {len(others_uval)}")
    print(f"Uniquely validated by *Nitwit*: {len(nitwit_uval)}")
    print(f"Validated by all, i.e. pretty sure these witnesses are correct: {len(all_val)}")
    print(f"Validated by none, i.e. pretty sure these witnesses are incorrect or too complex: {len(none_val)}\n")

    if diff_matching is not None:
        print(f"\nResults for diff:")
        diff_others, diff_cwv, diff_none, diff_all = analyze_unique_by_producer(diff_matching)
        print('-' * 40)
        print('Not in diff')
        print(f"Others: {others_uval.difference(diff_others)}")
        print(f"CWV: {nitwit_uval.difference(diff_cwv)}")
        print(f"None: {none_val.difference(diff_none)}")
        print(f"All: {all_val.difference(diff_all)}")
        print()
        print('Not in original')
        print(f"Others: {diff_others.difference(others_uval)}")
        print(f"CWV: {diff_cwv.difference(nitwit_uval)}")
        print(f"None: {diff_none.difference(none_val)}")
        print(f"All: {diff_all.difference(all_val)}")

    return others_uval, nitwit_uval, none_val, all_val


def reject_outliers(data, m=2):
    return data[abs(data - np.mean(data)) < m * np.std(data)]


times_data = {}
times_data_cols = ['Witnesses', 'Mean', 'Median', 'Std.dev.', 'Total']


def get_aggregated_data_table(results: List[str]):
    mux = pd.MultiIndex.from_product([VALIDATORS_LIST_ABBR, results], names=['Producer', 'Result'])
    df = pd.DataFrame(index=mux, columns=times_data_cols)
    for tup, val in times_data.items():
        df.loc[tup] = val

    if SAVE_TABLES:
        save_table_to_file(df.to_latex(float_format=lambda x: "{:.2f}".format(x)), 'aggregated_data', TABLE_DIR)


# print(df.to_latex(float_format=lambda x: "{:.2f}".format(x)))


def analyze_times(matching: Dict[str, dict], name: str, inclusion_predicate):
    print('=' * 20 + ' CPU TIME (s) ' + name + '=' * 20)
    # fig, axs = plt.subplots(nrows=2, ncols=3)
    figsc, axsc = plt.subplots()

    # take just the successful ones
    for i, val_index in enumerate(VALIDATORS.keys()):
        times = np.asarray(list(map(lambda x: float(x['results'][val_index]['cpu']),
                                    filter(lambda x: inclusion_predicate(get_status(x['results'], val_index)),
                                           matching.values()))))
        times = sorted(times)
        stats = get_stats_data(times)
        print(f"{VALIDATORS_ABBR[val_index]}: {stats}")
        times_data[(VALIDATORS_ABBR[val_index], name)] = stats

        sns.lineplot(ax=axsc, y=times, x=range(len(times)), label=VALIDATORS_ABBR[val_index])

    axsc.set_ylabel("Time [s]")
    axsc.set_xlabel(f"Number of witnesses ({name})")
    axsc.set(yscale='log')
    axsc.grid(True, which='both')
    # axsc.legend(loc=LEGEND_LOC_RIGHT, bbox_to_anchor=LEGEND_BBOX_ANCHOR, ncol=LEGEND_NCOL)
    axsc.axhline(90, color='black', lw=1, linestyle='--')
    # axsc.set_yticks([0, 20, 40, 60, 80, 90, 100])

    if SAVE_FIGURES:
        figsc.savefig(f'{FIGURE_DIR}/scatter_times_{name.lower()}.{FIGURE_FORMAT}', dpi=DPI,
                      bbox_inches=BBOX)
        plt.close(figsc)


def analyze_memory(matching: Dict[str, dict], name: str, inclusion_predicate):
    print('=' * 20 + ' MEMORY (MB) ' + name + '=' * 20)
    figsc, axsc = plt.subplots()
    # take just the successful ones
    for i, val_index in enumerate(VALIDATORS.keys()):
        mems = np.asarray(list(map(lambda x: float(x['results'][val_index]['mem']),
                                   filter(lambda x: inclusion_predicate(
                                       get_status(x['results'], val_index)),
                                          matching.values()))))
        print(f"{VALIDATORS_ABBR[val_index]}: {get_stats(mems)}")
        sns.lineplot(ax=axsc, y=sorted(mems), x=range(len(mems)), label=VALIDATORS_ABBR[val_index])

    axsc.set_ylabel("Memory [MB]")
    axsc.set_xlabel(f"Number of witnesses ({name})")
    axsc.set(yscale='log')
    axsc.grid(True, which='both')
    # axsc.legend(loc='upper left', bbox_to_anchor=(0.02, 0.9))
    axsc.axhline(7000, color='black', lw=1, linestyle='--')
    axsc.set_yticks([10, 100, 1000, 7000])
    if SAVE_FIGURES:
        figsc.savefig(f'{FIGURE_DIR}/scatter_memory_{name.lower()}.{FIGURE_FORMAT}', dpi=DPI,
                      bbox_inches=BBOX)
        plt.close(figsc)


def get_shared_result(first: int, second: int):
    res = ''
    if first == 0:
        res = f"{col_names[first]} (X)"
    elif second == 0:
        res = f"{col_names[second]} (Y)"
    else:
        res = 'Other'
    if first == second == 0:
        res = col_names[first]
    return res


res_labels = ['to', 'uk', 'tu', 'er', 'om', 'bw']


def get_index_in_res_labels(return_code: int) -> int:
    if return_code == 3:
        return 0
    elif return_code < 3:
        return return_code
    else:
        return return_code - 1


def adjust_to_scatter(tup: tuple):
    time1 = tup[0][0]
    time2 = tup[1][0]
    if tup[0][1] != 0:
        time1 = 90 + get_index_in_res_labels(tup[0][1]) * 4

    if tup[1][1] != 0:
        time2 = 90 + get_index_in_res_labels(tup[1][1]) * 4

    return [[time1, tup[0][1]], [time2, tup[1][1]]]


def compare_times(matching: Dict[str, dict]):
    # list of tuples (cpu, status) of nitwit
    nitwit = list(map(lambda x: (
        float(x['results'][NITWIT]['cpu']), int(get_status(x['results'], NITWIT))),
                      matching.values()))

    for val_key in VALIDATORS.keys():
        fig, ax = plt.subplots()
        times = list(
            map(lambda x: (
                float(x['results'][val_key]['cpu']), int(get_status(x['results'], val_key))),
                matching.values()))
        put_on_level = map(adjust_to_scatter, zip(nitwit, times))
        data_map = lambda tup: [tup[0][0], tup[1][0], get_shared_result(tup[0][1], tup[1][1])]
        data = list(map(data_map, put_on_level))
        df = pd.DataFrame(data, columns=['x', 'y', 'Result'])
        df = df.sort_values(by='Result')
        ax = sns.scatterplot(x='x', y='y', hue='Result', data=df, ax=ax, marker='x')

        for j, label in enumerate(res_labels):
            ax.axhline(90 + j * 4, color='black', lw=0.5, linestyle='--')
            ax.axvline(90 + j * 4, color='black', lw=0.5, linestyle='--')
            y_anno = ax.annotate(label, xy=(115, 90 + j * 4), xycoords='data', xytext=(5, -5),
                                 textcoords='offset points')
            y_anno.set_fontsize(10)
        x_offsets = [-6, -7, -5, -5, -6, -3]
        for j, label in enumerate(res_labels):
            x_anno = ax.annotate(label, xy=(90 + j * 4, 115), xycoords='data', xytext=(x_offsets[j], 5 + (j % 2) * 5),
                                 textcoords='offset points')
            x_anno.set_fontsize(10)

        ax.plot((0, 90), (0, 90), ls="-", c=".3")
        ax.plot((0, 63), (0, 90), ls="-", c=".3", lw=0.5)
        ax.plot((0, 90), (0, 63), ls="-", c=".3", lw=0.5)

        # axis label and description
        ax.set_xlabel(f"{VALIDATORS_ABBR[NITWIT]} CPU time [s]")
        ax.set_ylabel(f"{VALIDATORS_ABBR[val_key]} CPU time [s]")
        ax.set_yticks([0, 20, 40, 60, 80, 90])
        ax.set_xticks([0, 20, 40, 60, 80, 90])
        ax.legend(loc=LEGEND_LOC_RIGHT, bbox_to_anchor=LEGEND_BBOX_ANCHOR, ncol=LEGEND_NCOL)

        df_all_no_adjust = pd.DataFrame(list(map(data_map, zip(nitwit, times))), columns=['x', 'y', 'Result'])
        one_or_both_false = df_all_no_adjust[df_all_no_adjust['Result'] != 'Other']
        both_false = df_all_no_adjust[df_all_no_adjust['Result'] == 'False']
        df_diffs = (one_or_both_false['x'] - one_or_both_false['y'])
        df_diffs_both = (both_false['x'] - both_false['y'])
        df_diffs_all = df_all_no_adjust['x'] - df_all_no_adjust['y']
        print(
            f"Similar validations (+-1 s) with {VALIDATORS_ABBR[val_key]}: {df_diffs_both.apply(lambda x: -1 < x < 1).sum()}.\n")

        if SAVE_FIGURES:
            fig.savefig(f'{FIGURE_DIR}/quantile_times_{VALIDATORS_FILES[val_key]}.{FIGURE_FORMAT}',
                        dpi=DPI, bbox_inches=BBOX)
            plt.close(fig)


def get_stats(times) -> str:
    if len(times) == 0:
        return "Witnesses: 0"
    return f"Witnesses: {len(times)} Min: {np.min(times)} Max: {np.max(times)} Mean: {np.mean(times)} Median: {np.median(times)} Std dev: {np.std(times)} In total: {np.sum(times)}"


def get_stats_data(times) -> list:
    if len(times) == 0:
        return [0] * 5
    return [len(times), np.mean(times), np.median(times), np.std(times), np.sum(times)]


def output_val_data(val_data: Dict[str, dict]):
    output_data = []
    for w in val_data.values():
        output_data.append(list(map(lambda key: STATUSES[w['results'][key]['status']], VALIDATORS.keys())))

    df = pd.DataFrame(columns=VALIDATORS_LIST_ABBR, data=output_data, dtype=int)
    filename = f"data_{YEAR}.csv"
    with open(filename, 'w') as fp:
        df.to_csv(fp)
    print(
        f"Validated at least once: {len(df[(df == 0).sum(1) > 0])} ({((len(df[(df == 0).sum(1) > 0])) / len(val_data) * 100.0):.2f}%)")
    print(
        f"  Once: {len(df[(df == 0).sum(1) == 1])} ({((len(df[(df == 0).sum(1) == 1])) / len(val_data) * 100.0):.2f}%),")
    print(
        f"  Twice: {len(df[(df == 0).sum(1) == 2])} ({((len(df[(df == 0).sum(1) == 2])) / len(val_data) * 100.0):.2f}%),")
    print(
        f"  Thrice: {len(df[(df == 0).sum(1) == 3])} ({((len(df[(df == 0).sum(1) == 3])) / len(val_data) * 100.0):.2f}%),")
    print(
        f"  4 times: {len(df[(df == 0).sum(1) == 4])} ({((len(df[(df == 0).sum(1) == 4])) / len(val_data) * 100.0):.2f}%),")
    print(
        f"  5 times: {len(df[(df == 0).sum(1) == 5])} ({((len(df[(df == 0).sum(1) == 5])) / len(val_data) * 100.0):.2f}%)")
    if len(VALIDATORS_LIST_ABBR) >= 6:
        print(
            f"  6 times: {len(df[(df == 0).sum(1) == 6])} ({((len(df[(df == 0).sum(1) == 6])) / len(val_data) * 100.0):.2f}%)")
    if len(VALIDATORS_LIST_ABBR) >= 7:
        print(
            f"  7 times: {len(df[(df == 0).sum(1) == 7])} ({((len(df[(df == 0).sum(1) == 7])) / len(val_data) * 100.0):.2f}%)")
    # Fix missing initial column name
    with open(filename, 'r') as original:
        data = original.read()
    with open(filename, 'w') as modified:
        modified.write("Witness" + data)


def main():
    global SAVE_FIGURES, SAVE_TABLES, FIGURE_DIR, TABLE_DIR, VALIDATORS, VALIDATORS_ABBR, VALIDATORS_FILES, VALIDATORS_LIST_ABBR, NITWIT, YEAR
    parser = argparse.ArgumentParser(description="Analyzes results of NITWIT and SV-COMP validators")
    parser.add_argument("-v", "--validators", required=True, type=str,
                        help="The JSON file with results about SV-COMP validator runs (by witness hash).")
    parser.add_argument("-r", "--bench-results", required=False, type=str,
                        help="The directory with validation results of NITWIT generated by our benchmark script.")
    parser.add_argument("-om", "--output-val-data", required=False, type=str,
                        help="CSV file where to write the data so that they can be loaded in an R script that generates"
                             "a Venn diagram.")
    parser.add_argument("-df", "--diff", required=False, type=str,
                        help="Directory with other results to compare.")
    parser.add_argument("-s", "--save", required=False, nargs='?', const='t', default='f', type=str,
                        help="Save figures into thesis directory (default).")
    parser.add_argument("-t", "--table", required=False, nargs='?', const='t', default='f', type=str,
                        help="Create Tables from Latex-files and save them into thesis directory (default).")
    parser.add_argument("-g", "--graph", required=False, default=False, action='store_true', help="Show graphs.")
    parser.add_argument("-y", "--year", required=False, type=int,
                        help="The year to work on. Selects layout and validators.")
    args = parser.parse_args()

    # check whether argument was passed and how many parameters
    YEAR = 2020
    if args.year:
        YEAR = int(args.year)
    print(f"Using config for year: {YEAR}.")

    TABLE_DIR = f'./output/tables{YEAR}'
    FIGURE_DIR = f'./output/imgs{YEAR}'

    if args.save == 't':
        SAVE_FIGURES = True
        if not os.path.exists(FIGURE_DIR):
            os.mkdir(FIGURE_DIR)
    elif args.save != 'f':
        SAVE_FIGURES = True
        FIGURE_DIR = args.save

    if args.table == 't':
        SAVE_TABLES = True
        if not os.path.exists(TABLE_DIR):
            os.mkdir(TABLE_DIR)
    elif args.table != 'f':
        SAVE_TABLES = True
        TABLE_DIR = args.save

    all_validators = load_validators_result_file(args.validators)
    if all_validators is None:
        return 1

    # Setup globals depending on year
    if YEAR == 2020:
        NITWIT = "8"
        VALIDATORS = {
            "1": "CPAchecker",
            "3": "Ultimate Automizer",
            "4": "CPA-witness2test",
            "5": "FShell-witness2test",
            "7": "metaval",
            "8": "NITWIT Validator",
        }
        VALIDATORS_ABBR = {
            "1": "CPAchecker",
            "3": "Ult. Auto.",
            "4": "CPA-w2t",
            "5": "FShell-w2t",
            "7": "MetaVal",
            "8": "NITWIT"
        }
        VALIDATORS_FILES = {
            "1": "cpachecker",
            "3": "ua",
            "4": "cpaw2t",
            "5": "fsw2t",
            "7": "metaval",
            "8": "nitwit"
        }
        VALIDATORS_LIST_ABBR = ["CPAchecker", "Ult. Auto.", "CPA-w2t", "FShell-w2t", "MetaVal", "NITWIT"]
    elif YEAR == 2021:
        NITWIT = "8"
        VALIDATORS = {
            "1": "CPAchecker",
            "3": "Ultimate Automizer",
            "4": "CPA-witness2test",
            "5": "FShell-witness2test",
            "7": "MetalVal",
            "8": "NITWIT Validator",
        }
        VALIDATORS_ABBR = {
            "1": "CPAchecker",
            "3": "Ult. Auto.",
            "4": "CPA-w2t",
            "5": "FShell-w2t",
            "7": "MetaVal",
            "8": "NITWIT"
        }
        VALIDATORS_FILES = {
            "1": "cpachecker",
            "3": "ua",
            "4": "cpaw2t",
            "5": "fsw2t",
            "7": "metaval",
            "8": "nitwit"
        }
        VALIDATORS_LIST_ABBR = ["CPAchecker", "Ult. Auto.", "CPA-w2t", "FShell-w2t", "MetaVal", "NITWIT"]
    elif YEAR == 2022:
        NITWIT = "6"
        VALIDATORS = {
            "0": "CPA-witness2test",
            "2": "CPAchecker",
            "3": "FShell-witness2test",
            "5": "MetaVal",
            "6": "NITWIT Validator",
            "7": "Symbiotic-Witch",
            "9": "Ultimate Automizer"
        }
        VALIDATORS_ABBR = {
            "0": "CPA-w2t",
            "2": "CPAchecker",
            "3": "FShell-w2t",
            "5": "MetaVal",
            "6": "NITWIT",
            "7": "Symb.-Witch",
            "9": "Ult. Auto."
        }
        VALIDATORS_FILES = {
            "0": "cpaw2t",
            "2": "cpachecker",
            "3": "fsw2t",
            "5": "metaval",
            "6": "nitwit",
            "7": "symbwitch",
            "9": "ua"
        }
        VALIDATORS_LIST_ABBR = ["CPA-w2t", "CPAchecker", "FShell-w2t", "MetaVal", "NITWIT", "Symb.-Witch",
                                "Ult. Auto."]
    else:
        print(f"Error: Year {YEAR} is not supported!")
        exit(1)

    if args.bench_results:
        val, nval, bpar = load_result_files(args.bench_results)

        set_header_index(val[0])

        all_bench_results = val[1:] + nval[1:] + bpar[1:]
        all_validators = get_matching(all_bench_results, all_validators)

    if args.output_val_data:
        output_val_data(all_validators)  # Don't run every time, once is enough per benchmark

    # ANALYSES
    analyze_output_messages(all_validators)

    analyze_by_producer(all_validators)
    analyze_virt_best(all_validators)
    analyze_unique_by_producer(all_validators)

    for i, name in enumerate(col_names):
        analyze_times(all_validators, name, lambda x: x == i)

    analyze_times(all_validators, 'Other', lambda x: x > 2)
    analyze_times(all_validators, 'All', lambda x: True)
    get_aggregated_data_table(col_names + ['Other', 'All'])

    for i, name in enumerate(col_names):
        analyze_memory(all_validators, name, lambda x: x == i)
    analyze_memory(all_validators, 'Other', lambda x: x > 2)
    analyze_memory(all_validators, 'All', lambda x: True)

    compare_times(all_validators)

    if args.graph:
        plt.show()


if __name__ == "__main__":
    main()
