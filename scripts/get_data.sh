#!/bin/bash

# Source cluster-dependent variables
source "config.conf"

save_dir=$1

bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile}  --ssh_user=adb321  --get_data  --local_save_dir=${workspace}/benchmark/one_lock/results/${save_dir}/ --remote_save_dir=/users/adb321/alock/alock/bazel-bin/alock/benchmark/one_lock/main.runfiles/_main/${save_dir} --lock_type=${lock}