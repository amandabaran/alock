#!/bin/bash

# Source cluster-dependent variables
source "config.conf"

command() {
  tmp=$(pwd)
  cd ../../rome/scripts
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321/alock --local_root=/Users/amandabaran/Desktop/sss/async_locks/alock --cmd="$1"
  cd ${tmp}
  echo "Clean Complete\n"
}

command "pkill -9 -f bazel"