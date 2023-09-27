#!/bin/bash

# Source cluster-dependent variables
source "config.conf"

save_dir="exp5"
echo "Plotting results in ${save_dir}"

bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile}  --ssh_user=adb321  --plot  --local_save_dir=${workspace}/benchmark/one_lock/results/${save_dir}/ --remote_save_dir=${save_dir} --remote_save_dir=${save_dir}
