#!/usr/bin/env bash

if [[ $1 == "" || $1 == "-h" || $1 == "--help" ]]
then
  echo "Usage: ./validate.sh -v/--version
       ./validate.sh -w/--witness <witness> <program>
       ./validate.sh -64 -w/--witness <witness> <program>"
  exit 255
fi

if [[ $1 == "-v" || $1 == "--version" ]]
then
  echo "1.0"
  exit 0
fi

if [[ ! -d "bin" ]]
then
  echo "Build the tool first with build.sh"
  exit 255
fi

if [[ $1 == "-64" && ( $2 == "-w" || $2 == "--witness") && -e $3 && -e $4 ]]
then
  ./bin/nitwit64 $3 $4
elif [[ ( $1 == "-w" || $1 == "--witness") && -e $2 && -e $3 ]]
then
  ./bin/nitwit32 $2 $3
else
  echo "Files don't exist or bad arguments, see --help."
fi