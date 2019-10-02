# NITWIT Validator
An simple but fast interpreter-based violation witness validator for C code.

## License
NITWIT is governed by the New BSD license, but includes [PicoC](https://gitlab.com/zsaleeba/picoc), licensed under different terms. See [`LICENSE`](LICENSE) for more information.

## Prerequisites
 - Linux (for CPU + Memory measurements, though this dependency can be removed from main.cpp and compiled on any platform if required)
 - [CMake](https://cmake.org/) 3.10+
 - GCC and build tools, includin gcc/g++ multilib for 32bit validations

## Changing behaviour with compiler options
 - VERBOSE
 - NO_HEADER_INCLUDE
 - REQUIRE_MATCHING_ORIGINFILENAME
 - ENABLE_TRANSITION_LIMIT
 - STOP_IN_SINK

