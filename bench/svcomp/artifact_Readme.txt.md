# Artifact instructions

1. Create directory `shared` and change to it.
2. Unzip the artifact.
3. If you only want to regenerate the graphs based on recorded data from a previously recorded run, then skip to point #TODO.
4. Go to directory `data` with `cd data` and run `unzip sv-benchmarks-svcomp19.zip` and rename the resulting directory with `mv sv-benchmarks-svcomp19.zip sv-benchmarks`. This takes about 7 GB.
5. Go to directory `data/sv-witnesses` and unzip `witnesses-2019.zip`, this takes quite a while and needs about 50 GB.
6. Add the `shared` directory to the VirtualBox shared folders in Settings.
7. Start the `tacas20aec` VM and mount the shared folder to `Shared` as specified in the how-to.
8. Open a terminal and change to the `Shared` directory with `cd Shared`.
9. Install `pip3`, `gcc-multilib` and `g++-multilib` by running `sudo dpkg -i *.deb` from the prepared packages. Then install python3 utilities for data analysis and producing of graphs by `pip3 install *.whl`.
10. Switch to the validator directory with `cd nitwit` and build the validator with `./build.sh`. To use an unoptimized debug version of the validator which also outputs traces, run `./build.sh -debug`.
If you would like to run only a single validations, the best way is to use our wrapper script `nitwit.sh` provided in the project directory. Basic usage is as follows:
```
./nitwit.sh --witness <witness> <C-program>
```
Default CPU architecture is 32-bit, you can select 64-bit architecture with the flag `-64`.`
11. [Optional] Check your build with running basic tests `run-tests.sh cmake-build-release`.
12. Switch to directory `bench` with `cd bench`. Now, you can run a prepared configuration of validations available in `configs/reachability.json` or create your own. To skip creating the configuration, go to point 13. Else run the script:
```
python3 bench_config.py -w ../../data/sv-witnesses -sv ../../data/sv-benchmarks configs/my_reachability.json -ed ../../data/sv-validators/output.json
```
This script takes as inputs the directories of programs and witnesses. Additionally, it gets information about which witness maps to which program from extracted data saved in a JSON format. This we obtained by parsing HTML of categories in ReachSafety in the [results](https://sv-comp.sosy-lab.org/2019/results/results-verified/) of SV-COMP'19.
13. To run the validator on a preconfigured set of witnesses, use the script:
```
python3 bench_parallel.py  -w ../../data/sv-witnesses -e ../cmake-build-release/nitwit32 -sv ../../data/sv-benchmarks -to 2 -p 4 -c configs/reachability.json
```
With `-to N` you specify the allowed timeout in seconds. Option `-p N` you give the amount of validation processes to launch in parallel, which will take advantage of your CPUs multiprocessing power. Don't use more than your core count for better measurements. Option `-c CONFIG` specifies the configuration file to be used. You can additionally use the flag `-l N` to limit the number of validations. With `-l 1000` the first 1000 witnesses from the dataset of about 12000 would be inspected.

Note that the full configuration will run about 12 minutes on 4 cores clocked at approximately 1.8 GHz (Intel i5-8250U).  A machine with 3 GB of RAM per process will be more than enough. There are only a few cases when the validator exceeds 100 MB.
14. With a timeout of 2 seconds, you should get around 8500 successful validations. With a longer timeout, more violations are found as the processes are not killed prematurely.
15. The results will be saved in directory `output/nitwit<run>` where `<run>` is the architecture and date. To show a summary of these, run the following:
```
python3 bench_results_analysis.py -w ../../data/sv-witnesses -r output/nitwit<run>

```
This shows witnesses in categories of *Validated*, *Non-Validated* and *Badly-parsed* with extracted error messages.
There is also an already prepared set of results in directory `output/limit_best`, which was the basis for data presented in our paper. We will use these results in the following.
16. 
17. 
