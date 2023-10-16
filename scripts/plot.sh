#!/bin/bash

# Source cluster-dependent variables
source "config.conf"

save_dir="exp6"
lock="alock"
echo "Plotting results in ${save_dir}"

bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} --plot  --local_save_dir=${workspace}/benchmark/one_lock/results/${save_dir}/ --lock_type=${lock} --datafile=/Users/amandabaran/Desktop/sss/async_locks/alock/alock/alock/benchmark/one_lock/plots/output.csv
