#!/bin/bash

# Source cluster-dependent variables
source "config2.conf"

save_dir="node_scale20"
datafile="node_scale20.csv"
echo "Plotting results in ${save_dir}"

bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} --plot  --lock_type=$1 -local_save_dir=${workspace}/benchmark/one_lock/results/${save_dir}/ --datafile=/Users/amandabaran/Desktop/sss/async_locks/alock/alock/alock/benchmark/one_lock/plots/${datafile} --exp=${save_dir}
