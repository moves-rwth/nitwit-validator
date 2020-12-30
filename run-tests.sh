#!/usr/bin/env bash

# Usage: ./run-tests.sh <build-dir> <SV_comp-dir> <err_func_name>

if [[ -d $1 ]]
then
    cd $1 && make || exit 1
    cd ..
else
    echo "Error: build path non-existent"
    exit 1
fi

if [[ -d testfiles/$2 ]]
then
    echo "testdirectory existent!"
else
    echo $2
    echo "Error: test directory non-existent"
    exit 1
fi

if [[ $3 == __VERIFIER_error || $3 == reach_error ]]
then
	echo "valid error function found!"
else
    echo "Error: no error function name given"
    exit 1

fi

n_tests=0
n_nonvalidated=0
for C_FILE in testfiles/$2/*.c ; do
    [[ ! -f $C_FILE ]] && continue

    filename=$(basename -- "$C_FILE")
    extension="${filename##*.}"
    filename="${filename%.*}"

    for WITNESS in testfiles/$2/$filename.*.c.graphml ; do
        [[ -f "$WITNESS" ]] || break
        let "n_tests=n_tests+1"
        $1/nitwit32 $WITNESS $C_FILE $3> /dev/null

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

    for WITNESS in testfiles/$2/$filename.*.c.graphml.invalid ; do
        [[ -f "$WITNESS" ]] || break
        let "n_tests=n_tests+1"
        $1/nitwit32 $WITNESS $C_FILE $3> /dev/null

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


