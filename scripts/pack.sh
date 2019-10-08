#!/bin/bash

if [[ ! -e "nitwit.sh" ]]
then
		echo "Run this from root directory of NITWIT Validator."
		exit 1
fi

./build.sh
zip val_nitwit.zip nitwit.sh bin/nitwit*
