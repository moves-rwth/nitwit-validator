# Artifact instructions
This file describes the contents of the `NITWIT_TACAS20.zip` artifact which contains a reproduction set for the paper
*Interpretation-Based Violation Witness Validation for C: NITWIT*. There are multiple ways to run the NITWIT validator
presented in the paper. Likely the easiest is to build a Docker image, which you can later purge without changes to the
system. Should you want to reproduce results presented in our paper, please follow the steps bellow.

## Availability
This artifact will be permanently archived on a data service *Zenodo* at this [link](#TODO). 

## Contents
The artifact contains:
- This `Readme.txt`.
- License `License.txt`.
- The artifact reproduction set in `artifact`.
- Source code of NITWIT Validator in directory `artifact/nitwit`.
- Scripts for running benchmars and producing graphs and tables in `artifact/nitwit/bench`.
- Configuration files that define which witnesses to run in a benchmark in `artifact/nitwit/bench/configs`.
- The recorded data from a benchmark run we have presented in the paper in `artifact/nitwit/bench/output/limit_best`.
- Programs ([source](https://github.com/sosy-lab/sv-benchmarks/archive/svcomp19.zip)) and witnesses ([source](https://zenodo.org/record/2559175))
  from SV-COMP'19 in `artifact/data/sv-benchmarks` and `artifact/data/sv-witnesses` (needs unzipping, steps described
  below).
- Data on resource usage we have obtained from [SV-COMP'19 results](https://sv-comp.sosy-lab.org/2019/results/results-verified/)
  about validations in category ReachSafety from other validators. This file is in `artifact/data/sv-validators/output.json`. 
- All of the necessary software that needs to be additionally installed on the TACAS virtual machine in order to build
  and run the validator. Python packages for producing graphs are also included. Installation instruction are part of 
  the reproduction manual. 

## Licenses
Our validator has the New BSD license as specified by `License.txt`. The data has different licensing (though also
open). Please see the provided links or license files in their particular directory/archive for more details.

## Steps to reproduce
1. Download the artifact [from this website](#TODO).
2. Create directory `vb_shared` and change to it.
3. Unzip the artifact and go to the main directory with `cd artifact`.
4. Go to directory `data` with `cd data` and run `unzip sv-benchmarks-svcomp19.zip`. Then rename the extracted directory
   with `mv sv-benchmarks-svcomp19.zip sv-benchmarks`. This takes about 7 GB.
5. Go to directory `data/sv-witnesses` and unzip `witnesses-2019.zip`, this takes a few minutes (with an SSD) and needs 
   about 50 GB.
6. Add the `vb_shared` directory to the VirtualBox shared folders in Settings.
7. Start the `tacas20aec` VM and mount the shared folder to `vb_shared` as specified in a how-to file inside the machine.
8. Open a terminal and change to the directory with additional software using `cd Shared/artifact/packages`.
9. Install `pip3`, `gcc-multilib` and `g++-multilib` by running `sudo dpkg -i *.deb` from the prepared packages. Then 
   install Python3 utilities for data analysis and producing of graphs by `pip3 install *.whl`.
10. Switch to the validator directory with `cd ../nitwit` and build the validator with `./build.sh`. To use an 
    unoptimized debug version of the validator which also outputs traces, run `./build.sh -debug`. If you would like to
    run only a single validation, use our wrapper script `nitwit.sh` provided in the project directory. Basic usage is 
    as follows:
    ```
    ./nitwit.sh --witness <witness> <C-program>
    ```
    Default CPU architecture is 32-bit, you can select 64-bit architecture with the flag `-64`. You can specify the
    property file with `-p <file>` though note that NITWIT supports only `CHECK( init(main()), LTL(G ! call(__VERIFIER_error())) )`
    so it is set defaultly.
11. \[Optional\] Check your build with running basic tests `run-tests.sh cmake-build-release` (or
    `run-tests.sh cmake-build-debug` if you built the debug version).
12. Switch to directory `bench` with `cd bench`. 
12. \[Optional\] You can run a prepared configuration of validations available in `configs/reachability.json` or create
    your own. To skip creating the configuration, go to point 13. Else run the script:
    ```
    python3 bench_config.py -w ../../data/sv-witnesses -sv ../../data/sv-benchmarks configs/my_reachability.json -ed ../../data/sv-validators/output.json
    ```
    This script takes as inputs the directories of programs and witnesses. Additionally, it gets information about which
    witness maps to which program from extracted data saved in a JSON format.
13. To run the validator on a preconfigured set of witnesses, use the script:
    ```
    python3 bench_parallel.py  -w ../../data/sv-witnesses -e ../cmake-build-release/nitwit32 -sv ../../data/sv-benchmarks -to 2 -p 4 -c configs/reachability.json
    ```
    With `-to <secs>` you specify the allowed timeout in seconds. Option `-p <procs>` you give the amount of validation
    processes to launch in parallel, which will take advantage of your CPUs multiprocessing power. Don't use more than 
    your core or thread count. Option `-c <config>` specifies the configuration file to be used. You can additionally
    use the flag `-l <limit>` to limit the number of validations. With `-l 1000` the first 1000 witnesses from the dataset of
    about 12000 would be inspected.
    
    Note that the parallel run shown above will run about 12 minutes on 4 cores clocked at approximately 1.8 GHz (Intel
    i5-8250U). The runtime highly depends on the set timeout limit. A machine with 3 GB of RAM per process will be more than
    enough. There are only a few cases when the validator exceeds 100 MB.
    With a timeout of 2 seconds, you should get around 8500 successful validations depending on your CPU. 
    With a longer timeout, more violations are found as some processes are not killed prematurely.
14. \[Optional\] The results will be saved in directory `artifact/nitwit/bench/output/nitwit<run>` where `run` is the architecture
    and date. To show a summary of these, run the following:
    ```
    python3 bench_results_analysis.py -w ../../data/sv-witnesses -r output/nitwit<run>
    ```
    This shows witnesses in categories of *Validated*, *Non-Validated* and *Badly-parsed* with extracted error messages.
    There is also an already prepared set of results in directory `output/limit_best` you can inspect. This was the
    basis for data presented in our paper. Directory `output/best` contains results for when transition limiting was 
    disabled which lead to about 25 more found validations.
15. \[Optional\] If you would like to repeat a single validation from the benchmark, open the file in 
    `output/limit_best/<category>_witnesses.json` and find the validation task based on either the exit code, witness
     hash, error message, runtime (seconds), producer or memory used (bytes). Copy the witness hash and run:
     ```
     python3 exec_single.py -w ../../data/sv-witnesses -sv ../../data/sv-benchmarks -e ../cmake-build-release/nitwit32 -to 2 -f <hash>.json
     ```
     With flag `-e <exec>` you choose the executable (depending whether you want to use the debug/release version and 
     32/64-bit architecture). Option `-to <secs>` again sets the runtime limit and `-f <witness-hash>` sets the witness
     to find and execute.
16. To display graphs, output tables and statistical data, run the following from the `bench` directory:
    ```
    python3 validator_analysis.py -v ../../data/sv-validators/output.json -r output/limit_best -g
    ```
    You can supply any results produced during `bench_parallel.py` with the `-r` flag. Here we used the results as 
    presented in the paper, but change it accordingly if you wish to analyze a benchmark run you did. Option `-g` shows
    the graphs in interactive *matplotlib* windows. Only a subset of all of the graphs that will be shown to you with
    this script was included in the paper due to page space constraints. 
