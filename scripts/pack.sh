#!/bin/bash

if [[ ! -e "nitwit.sh" ]]
then
		echo "Run this from root directory of NITWIT Validator."
		exit 1
fi

./build.sh

if [[ -e "/tmp/nitwit" ]]
then
    echo "Clean the directory /tmp/nitwit"
    exit 1
fi

mkdir /tmp/nitwit
cp --parents nitwit.sh bin/nitwit* LICENSE picoc/LICENSE /tmp/nitwit/.
zip -r val_nitwit.zip /tmp/nitwit/*
