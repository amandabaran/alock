#!/bin/bash

# Source cluster-dependent variables
source "config.conf"

save_dir=$1

bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile}  --ssh_user=adb321  --get_data   --lock_type=${lock} --local_save_dir=${workspace}/benchmark/one_lock/results/${save_dir}/ --remote_save_dir=/users/adb321/results/${save_dir}
echo "Data Collection Complete\n"

command() {
  tmp=$(pwd)
  cd ../../rome/scripts
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321/alock --local_root=/Users/amandabaran/Desktop/sss/async_locks/alock --cmd="$1"
  cd ${tmp}
  echo "Data Deletion Complete\n"
}

command "rm -rf /users/adb321/results/${save_dir}"
