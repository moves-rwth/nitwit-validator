#!/usr/local_rwth/bin/zsh
# ask for 1 GB memory
#SBATCH --mem-per-cpu=700M   #M is the default and can therefore be omitted, but could also be K(ilo)|G(iga)|T(era)
#SBATCH --job-name=VALBENCH
# declare the merged STDOUT/STDERR file
#SBATCH --output=VALBENCH.%J.txt
#SBATCH --nodes=1
#SBATCH --ntasks-per-core=1
#SBATCH --ntasks-per-node=48
#SBATCH --time=02:00:00

time python3 python3 bench_parallel.py  -w ../../data/sv-witnesses -e ../cmake-build-release/cwvalidator -sv ../../data/sv-benchmarks -to 60 -ex output/cwvalidator-b2c32f9_2019-05-10_14-03-57/badly_parsed_witnesses.json
echo "Done!"