#!/usr/bin/env bash

PROPERTY_FILE=""
WITNESS_FILE=""
PROGRAM=""
ARCHITECTURE="-32"

if [[ "$1" == "" ]]
then
  echo "See -h/--help."
  exit 255
fi

while [ -n "$1" ] ; do
  case "$1" in
    -32|-64) ARCHITECTURE="$1" ; shift 1 ;;
    -p|--property) PROPERTY_FILE="$2" ; shift 2 ;;
    -w|--witness) WITNESS_FILE="$2" ; shift 2 ;;
    -v|--version)
      echo "1.0" ; exit 0 ;;
    -h|--help)
      echo "Usage: ./nitwit.sh -v/--version
       ./nitwit.sh -w/--witness <witness-file> [-p/--property] <property-file> <C-program>
       ./nitwit.sh -32/-64 --witness <witness> <C-program>  # 32-bit archite is default";
     exit 0 ;;
    *) PROGRAM="$1" ; shift 1 ;;
  esac
done

if [[ ! -d "bin" ]]
then
  echo "Build the tool first with build.sh"
  exit 255
fi

if [[ ! (-e $PROGRAM && -e $WITNESS_FILE ) ]]
then
  echo "Witness and/or program file(s) not found/provided."
  exit 255
fi

if [[ "$PROPERTY_FILE" != "" ]]
then
  PROPERTY_STRING=`cat $PROPERTY_FILE | tr -d "[:space:]"`
  if [[ $PROPERTY_STRING != "CHECK(init(main()),LTL(G!call(__VERIFIER_error())))" ]]
  then
    echo "Not supported property ${PROPERTY_STRING}. Nitwit only supports reachability safety."
    exit 255
  fi
fi


SUFFIX="${ARCHITECTURE:1:2}"
./bin/nitwit$SUFFIX $WITNESS_FILE $PROGRAM
