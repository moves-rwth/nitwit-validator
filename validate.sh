#!/usr/bin/env bash

# Usage: ./validate.sh -v/--version
#        ./validate.sh -w/--witness <witness> <program>
#        ./validate.sh -64 -w/--witness <witness> <program>

if [[ $1 == "-v" || $1 == "--version" ]]
then
  git rev-parse --short HEAD && exit 0
fi

if [[ ! -d "cmake-build-release" ]]
then
  echo "Build the tool in cmake-build-release first."
  exit 255
fi

if [[ $1 == "-64" && ( $2 == "-w" || $2 == "--witness") && -e $3 && -e $4 ]]
then
  ./cmake-build-release/cwvalidator64 $3 $4
elif [[ ( $1 == "-w" || $1 == "--witness") && -e $2 && -e $3 ]]
then
  ./cmake-build-release/cwvalidator32 $2 $3
fi