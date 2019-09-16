#!/usr/bin/env bash

# Usage: ./validate.sh -v/--version
#        ./validate.sh <witness> <program>
#        ./validate.sh -64 <witness> <program>

if [[ ! -d "cmake-build-release" ]]
then
  echo "Build the tool in cmake-build-release first."
fi

if [[ $1 == "-v" || $1 == "--version" ]]
then
  git rev-parse --short HEAD && exit 0
fi

if [[ $1 == "-64" && -e $2 && -e $3 ]]
then
  ./cmake-build-release/cwvalidator64 $2 $3
elif [[ -e $1 && -e $2 ]]
then
  ./cmake-build-release/cwvalidator32 $1 $2
fi