#!/usr/bin/env bash

if [[ ! -f $1 ]]
then
    echo "Provide a file"
    exit 1
fi

#cbmc --32 $1 --graphml-witness $1bmc.c.graphml

cpa -config ../../cpa/CPAchecker-1.8-unix/config/kInduction.properties -spec reach.prp -preprocess $1 -32
#cpa -config ../../cpa/CPAchecker-1.8-unix/config/predicateAnalysis-slicing.properties -spec reach.prp -preprocess $1 -32

if [[ $? -eq 0 ]]
then
    cd output && \
    gunzip Counterexample.1.graphml.gz && \
    mv Counterexample.1.graphml ../$1pa.c.graphml && \
    cd .. && \
    rm -rf output
fi
