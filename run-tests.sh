#!/usr/bin/env bash

# Usage: ./test.sh <build-dir>

if [[ -d $1 ]]
then
    cd $1 && make || exit 1
    cd ..
else
    echo "Error: build path non-existent"
    exit 1
fi

n_tests=0
n_nonvalidated=0
for C_FILE in testfiles/*.c ; do
    [[ ! -f $C_FILE ]] && continue

    filename=$(basename -- "$C_FILE")
    extension="${filename##*.}"
    filename="${filename%.*}"

    for WITNESS in testfiles/$filename.*.c.graphml ; do
        [[ -f "$WITNESS" ]] || break
        let "n_tests=n_tests+1"
        $1/cwvalidator $WITNESS $C_FILE > /dev/null

        exit_val=$?
        if [[ ${exit_val} -eq 0 || ${exit_val} -eq 245 ]]
        then
            echo "Validated witness: $WITNESS"
        elif [[ ${exit_val} -ge 240 && ${exit_val} -le 250 ]]
        then
            let "n_nonvalidated=n_nonvalidated+1"
            echo -e "\e[31mNon-validated witness: $WITNESS ($exit_val)\e[0m"
        elif [[ $exit_val -eq 2 ]]
        then
            let "n_nonvalidated=n_nonvalidated+1"
            echo -e "\e[31mWitness parse error: $WITNESS\e[0m"
        elif [[ $exit_val -eq 3 ]]
        then
            let "n_nonvalidated=n_nonvalidated+1"
            echo "Warning: Bad usage: $WITNESS"
        else
            let "n_nonvalidated=n_nonvalidated+1"
            echo -e "\e[31mOther error: $WITNESS, exit value: $exit_val\e[0m"
        fi
    done

    for WITNESS in testfiles/$filename.*.c.graphml.invalid ; do
        [[ -f "$WITNESS" ]] || break
        let "n_tests=n_tests+1"
        $1/cwvalidator $WITNESS $C_FILE > /dev/null

        exit_val=$?
        if [[ ${exit_val} -ne 0 ]]
        then
            echo "Correctly denied witness: $WITNESS ($exit_val)"
        else
            let "n_nonvalidated=n_nonvalidated+1"
            echo -e "\e[31mIncorrectly validated: $WITNESS, exit value: $exit_val\e[0m"
        fi
    done

done

echo "Tests done: $n_tests, non-validated: $n_nonvalidated"

if [[ $n_nonvalidated -gt 0 ]]
then
    exit 1
fi


