#!/bin/bash

if [[ ! -e "nitwit.sh" ]]
then
		echo "Run this from root directory of NITWIT Validator."
		exit 1
fi

./build.sh

mkdir nitwit
cp --parent nitwit.sh bin/nitwit* LICENSE picoc/LICENSE nitwit/.
zip -r val_nitwit.zip nitwit/*
