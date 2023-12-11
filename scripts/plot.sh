#!/bin/bash

source "config.conf"

# Source cluster-dependent variables
save_dir="nodescale"
datafile="${save_dir}.csv"
echo "Plotting results in ${save_dir}"

bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} --plot -local_save_dir=${workspace}/benchmark/one_lock/results/${save_dir}/ --datafile=/Users/amandabaran/Desktop/sss/async_locks/alock/alock/alock/benchmark/one_lock/plots/${datafile} --exp=${save_dir}
