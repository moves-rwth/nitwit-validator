# NITWIT Validator
A simple but fast interpreter-based violation witness validator for C code.

## License
NITWIT is governed by the New BSD license, but includes [PicoC](https://gitlab.com/zsaleeba/picoc), licensed under different terms. See [`LICENSE`](LICENSE) for more information.

## Prerequisites
 - Linux (for CPU + Memory measurements, though this dependency can be removed from main.cpp and compiled on any platform if required)
 - [CMake](https://cmake.org/) 3.10+
 - GCC and build tools, including gcc-multilib and g++-multilib for 32bit validations

## Changing behaviour with compiler options
 - VERBOSE
 - NO_HEADER_INCLUDE
 - REQUIRE_MATCHING_ORIGINFILENAME
 - ENABLE_TRANSITION_LIMIT
 - STOP_IN_SINK

## Building & Usage
  For building NITWIT, we require support for building 32bit applications on 64bit systems, so packages like `gcc-multilib` and `g++-multilib` are necessary.
 - To build (in the root directory):
    ```./build.sh```
 - To execute a basic test suite:
    ```./run-tests.sh cmake-build-release```
  
 - Then, to run a whole benchmark on a config with a 3s timeout and four parallel processes (in directory ./bench):
	``` 
	python3 bench_parallel.py  -w ../../data/sv-witnesses -e ../cmake-build-release/nitwit32 -sv ../../data/sv-benchmarks -to 3 -p 4 -c configs/reachability.json
	```

  This will output configuration files into `./bench/output/<run>/*.json`. These then can be processed to export graphs, flag `-g` shows them in individual windows, `-s` saves them to disk (right now the output directory is hardcoded), without `-g` or `-s` it just shows tables and numbers. (in ./bench)
	```
	python3 validator_analysis.py -v ../../data/sv-validators/output.json -r output/limit_best -g
	```

 - If you want to run a single validation over a specific witness from SV-COMP there is the script (in directory ./bench):
	```
	python3 exec_single.py -w ../../data/sv-witnesses -sv ../../data/sv-benchmarks -e ../cmake-build-debug/nitwit -to 2 -f 224b537066067d2f651860c9173fc6e514ca0e56f344f174bd292ab042325cca.json  
	```
	You specify the executable with `-e` which lets you take either the debug binary (outputs verbose info) or the optimized release binary. 
	With `-f` you can specify the witness hash.
	All of the script parameter parsing is done via `argparse` so it will give you help messages for the parameters. 

 - For running Nitwit from the wrapper script, you would do:
	``` (in root, position of parameters must be in the order as shown here)
	./nitwit.sh -v
	./nitwit.sh -w witness.graphml program.c
	./nitwit.sh -64 -w witness.graphml program.c
	```

## Output codes
 - 0   -> Successful validation. Violation found.
 - 1   -> (Not in use anymore.)
 - 2   -> Parsing witness failed (file not found).
 - 3   -> Usage error (wrong arguments).
 - 4   -> Unspecified error, probably parsing of C.

### Some more specific exit codes
 - 240 -> Witness missing.
 - 241 -> Witness got into a sink state.
 - 242 -> PROGRAM_FINISHED
 - 243 -> WITNESS_IN_ILLEGAL_STATE
 - 244 -> A C identifier was undefined (usually when some C function/type cannot be resolved).
 - 245 -> The program called __VERIFIER_error, but the witness automaton did not finish in an error state.
 - 246 -> A C identifier was defined twice.
 - 247 -> Unsupported nondeterministic operation (deprecated).
 - 248 -> __VERIFIER_assume or assert() failed.
 - 249 -> Bad function definition.
 - 250 -> Witness reached an error state, but __VERIFIER_error was not called.
 - 251 -> Out of memory.

## FAQ

### I get an error during compilation like ```/usr/include/c++/8/cstdio:41:10: fatal error: bits/c++config.h: No such file or directory```
	You are missing the packages for building 32bit applications! On Debian/Ubuntu for example, you need to install ```apt install gcc-multilib g++-multilib```.