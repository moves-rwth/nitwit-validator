# NITWIT Validator
A simple but fast interpreter-based violation witness validator for C code.
It validates violation witnesses in both the GraphML based format 1.0 and the YAML based format 2.x, see [Witness format 2.x](#witness-format-2x-yaml).

## Why the name?
The name is an acronym: iNterpretation-based vIolaTion WITness Validator

Since the tool does not use any advanced modelchecking techniques, there may be an error state hiding in the bushes, but NITWIT will only follow down one path.

## License
NITWIT is governed by the New BSD license, but includes [PicoC](https://gitlab.com/zsaleeba/picoc), [pugixml](https://pugixml.org/) and [yaml-cpp](https://github.com/jbeder/yaml-cpp), licensed under different terms. See [`LICENSE`](LICENSE) for more information.

## Prerequisites
 - Linux (for CPU + Memory measurements, though this dependency can be removed from main.cpp and compiled on any platform if required)
 - [CMake](https://cmake.org/) 3.10+
 - GCC and build tools, including gcc-multilib and g++-multilib for 32bit validations

## Changing behaviour with compiler options
 - VERBOSE (default on for debug, off for release) - outputs more info about program trace
 - NO_HEADER_INCLUDE (default off) - ignores any "extern" declarations and automatically includes all available libraries
 - REQUIRE_MATCHING_ORIGINFILENAME (default off) - matches edges also depending on the "originfilename" argument 
 - ENABLE_TRANSITION_LIMIT (default on) - stops NITWIT after a certain limit of unsuccessful edge transitions is made
 - STOP_IN_SINK (default off) - terminate the validation once the sink state is reached
 - USE_BASIC_CONST (default off) - enable parsing of const keyword (not full C semantics supported), otherwise ignore
 - STRICT_VALIDATION - disallows traces not accepted by the witness automaton  

## Building & Usage
  For building NITWIT, we require support for compiling 32bit applications on 64bit systems, so packages like `gcc-multilib` and `g++-multilib` are necessary.
 - To build (in the root directory):
    ```./build.sh```
 - To execute a basic test suite on the data from SV-comp 2019:
    ```./run-tests.sh cmake-build-release sv-comp-2019 __VERIFIER_error```
 - To execute the tests for features of the witness format 2.x not covered by the witnesses above:
    ```./run-tests.sh cmake-build-release yaml-witnesses reach_error```

   Both run `nitwit32`; set `NITWIT_BINARY=nitwit64` to test the 64bit executable instead.
  
 - Then, to run a whole benchmark on a config with a 3s timeout and four parallel processes (in directory ./bench):
	``` 
	python3 bench_parallel.py  -w ../../data/sv-witnesses -e ../cmake-build-release/nitwit32 -sv ../../data/sv-benchmarks -err __VERIFIER_error -to 3 -p 4 -c configs/reachability.json
	```

  This will output configuration files into `./bench/output/<run>/*.json`. These then can be processed to export graphs and tables, flag `-g` shows all graphs in individual windows, `-s` saves the graphs to disk (output directory is optional), `-t` saves the tables to disk (output directory is optional), without `-g` or `-s` or `-t` it just shows tables and numbers. (in ./bench)
	```
	python3 validator_analysis.py -v ../../data/sv-validators/output.json -r output/limit_best -g
	```

 - If you want to run a single validation over a specific witness from SV-COMP there is the script (in directory ./bench):
	```
	python3 exec_single.py -w ../../data/sv-witnesses -sv ../../data/sv-benchmarks -e ../cmake-build-debug/nitwit32 -err __VERIFIER_error -to 2 -f 224b537066067d2f651860c9173fc6e514ca0e56f344f174bd292ab042325cca.json  
	```
	You specify the executable with `-e` which lets you take either the debug binary (outputs verbose info) or the optimized release binary. 
	With `-f` you can specify the witness hash and with `-err` you can define the name of the validators error function.
	All of the script parameter parsing is done via `argparse` so it will give you help messages for the parameters. 

 - For running Nitwit from the wrapper script, you would do:
	``` (in root, position of parameters must be in the order as shown here)
	./nitwit.sh -v
	./nitwit.sh -w witness.graphml [-p/--property <property-file>] program.c
	./nitwit.sh -64 -w witness.graphml program.c
	./nitwit.sh -w witness.yml program.c
	```
	The witness format is detected from the content of the witness file, not from its name.

## Witness format 2.x (YAML)
NITWIT reads violation witnesses in the [witness format 2.x](https://gitlab.com/sosy-lab/benchmarking/sv-witnesses/-/blob/main/user-guide/Witness-Format.md) (versions 2.0 to 2.3, for C programs).
The `violation_sequence` is translated into a witness automaton with one state per segment, which is then matched against the execution just like a GraphML witness:
the follow waypoint of a segment leads to the next state, its avoid waypoints lead to the sink state, and the target waypoint of the last segment leads to the violation state.

 - `assumption` waypoints have to hold before the statement starting in their line is executed. As with GraphML witnesses, an equality like `x == 5` for a variable holding a nondeterministic value resolves the value.
 - `branching` waypoints are matched at the line of the `if`/`while`/`do`/`for` keyword (or the `?` of a conditional operator), even if the condition spans several lines. Branchings of `switch` statements are only matched by their line.
 - `function_enter` and `function_return` waypoints match the entry into and the return from any function called in their line. The `\result` constraint of a `function_return` waypoint resolves the value returned by `__VERIFIER_nondet_X()`; relations other than `==` to an integer constant are replaced by an equality with a value satisfying them (e.g. `\result > 5` becomes `\result == 6`). For other functions, the constraint is not checked.
 - `target` waypoints are matched in their line; as for GraphML witnesses, the validation succeeds when the error function is called.

Deviations from the format and limitations:
 - Locations are matched by their line only, columns are ignored. Several waypoints in the same line are distinguished by the order of their segments.
 - As for GraphML witnesses, a follow waypoint does not have to be passed the first time it is evaluated: the automaton stays in its state until the waypoint matches.
 - `avoid` waypoints of type `assumption` or `function_return` are ignored, as checking their constraint might resolve nondeterministic values.
 - The `metadata` of the witness (e.g. `data_model`, the architecture is chosen by the option `-32`/`-64` of `nitwit.sh` instead), `file_name` and the `format` of constraints are not checked.
 - NITWIT does not search for values the witness does not provide. For example, the 2.x witnesses of CPAchecker do not contain the values of nondeterministic calls used directly in a condition or of uninitialized variables, so NITWIT executes such programs with default values.
 - Witnesses NITWIT cannot validate result in UNKNOWN (code 4): correctness witnesses (`invariant_set`, `ghost_instrumentation`), `cycle` waypoints (non-termination), waypoints of threads other than the main thread (`thread_id`) and segments with several target waypoints (data races).

## Output codes
 - 0   -> Successful validation. Violation found.
 - 1   -> (Not in use anymore.)
 - 2   -> Parsing witness failed (file not found, malformed or invalid witness).
 - 3   -> Usage error (wrong arguments).
 - 4   -> Unspecified, probably failed in parsing C. Also used for witnesses that are not violation witnesses or use unsupported features.
 - 5   -> Witness reached an error state, but the error function was not called (deprecated, code 250 instead).

### Some more specific exit codes
 - 240 -> Witness missing (deprecated).
 - 241 -> Witness got into a sink state.
 - 242 -> Program finished without calling error function, witness not in an error state.
 - 243 -> Witness is in an illegal state.
 - 244 -> A C identifier was undefined (usually when some C function/type cannot be resolved).
 - 245 -> The program called error function, but the witness automaton did not finish in an error state.
 - 246 -> A C identifier was defined twice.
 - 247 -> Unsupported nondeterministic operation for resolving variables (currently only = and == are allowed in assumptions).
 - 248 -> error function or assert() failed.
 - 249 -> Bad function definition.
 - 250 -> Witness reached an error state, but error function was not called.
 - 251 -> Out of memory.
 - 255 -> Wrapper script error.

## Docker
For ease of usage, we provide a Docker image for our tool. To build it, install Docker and run
`docker build . -t nitwit:latest` in the project root directory.
Docker builds NITWIT in a Ubuntu 20.04 container and builds it with debug configuration enabled.
This lets you see the explored trace and resolved assumptions during validation.

As an example, suppose you would like to validate a witness from SV-COMP'19, for instance for the following recursive program:
```[C]
extern void __VERIFIER_error() __attribute__ ((__noreturn__));

/*
 * Recursive computation of fibonacci numbers.
 * 
 * Author: Matthias Heizmann
 * Date: 2013-07-13
 * 
 */

extern int __VERIFIER_nondet_int(void);


int fibonacci(int n) {
    if (n < 1) {
        return 0;
    } else if (n == 1) {
        return 1;
    } else {
        return fibonacci(n-1) + fibonacci(n-2);
    }
}


int main() {
    int x = __VERIFIER_nondet_int();
    int result = fibonacci(x);
    if (x < 8 || result >= 34) {
        return 0;
    } else {
        ERROR: __VERIFIER_error();
    }
}
    
```
available [here](https://raw.githubusercontent.com/sosy-lab/sv-benchmarks/svcomp19/c/recursive/Fibonacci05_false-unreach-call_true-no-overflow_true-termination.c).
Download this file to a directory on your computer. We will use a witness from CPAChecker which you can download [here](https://sv-comp.sosy-lab.org/2019/results/witnessFileByHash/a9f053b1405a441b2c52964a24c4192e5c0b78522578d59a62865c8339abc9ff.graphml).
Put both files in the same directory, for simplicity we assume they are named `main.c` and `witness.graphml`.

Now in the same directory, execute the following command:
```
docker run --rm --name nitwit -v `pwd`:/nitwit/testfiles nitwit:latest -32 -w testfiles/witness.graphml testfiles/main.c __VERIFIER_error
```
This will run NITWIT inside Docker to validate the program with semantics of a 32-bit CPU architecture.
You could also specify the option `-64` instead to switch to 64-bit architecture.
Alongside the simulation, NITWIT takes advantage of the witness to resolve variable `x` to value 8 as specified in the witness on edge `A8142 --> A8140`.
You should see output similar to:
```
Witness automaton reconstructed
============Start simulation============
--- Line: 14, Pos: 0
--- Line: 25, Pos: 0
	Taking edge: A7545 --> A8142
--- Line: 26, Pos: 33, Enter: __VERIFIER_nondet_int
Unmet assumption x == (8);.
--- Line: 26, Pos: 33
Unmet assumption x == (8);.
--- Line: 26, Pos: 34, Return: __VERIFIER_nondet_int
Unmet assumption x == (8);.
--- Line: 26, Pos: 34
Unmet assumption x == (8);.
--- Line: 26, Pos: 0
Resolved var: x: ---> 8
Assumption x == (8); satisfied.
	Taking edge: A8142 --> A8140
--- Line: 27, Pos: 26, Enter: fibonacci

       ...
(omitted for brevity)
       ...

--- Line: 28, Pos: 26, Control: 0
Assumption result == (21); satisfied.
	Taking edge: A7551 --> A7548
--- Line: 31, Pos: 31, Enter: __VERIFIER_error
--- Line: 31, Pos: 31
--- Line: 31, Pos: 32, Return: __VERIFIER_error
__VERIFIER_error has been called!
===============Finished=================
Stopping the interpreter.

VALIDATED: The state: A7548 has been reached. It is a violation state.
``` 

As indicated by the output, NITWIT was able to validate the witness.
If you would like to use NITWIT as a validation tool more extensively, we recommend to compile it directly on the machine where it will run with enabled compiler optimizations.
The easiest way is to run `./build.sh` after cloning this repository. 

## FAQ

### I get an error during compilation like ```/usr/include/c++/8/cstdio:41:10: fatal error: bits/c++config.h: No such file or directory```
You are missing the packages for building 32bit applications! On Debian/Ubuntu for example, you need to install ```apt install gcc-multilib g++-multilib```.

## Desirable C features that will be implemented in the future
- typedef of function pointers
- full const support
