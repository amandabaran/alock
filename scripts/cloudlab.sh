#!/bin/bash

workspace=/Users/amandabaran/Desktop/sss/async_locks/alock/alock/alock
nodefile=/Users/amandabaran/Desktop/sss/async_locks/alock/alock/alock/benchmark/nodefiles/r6525.csv

#** FUNCTION DEFINITIONS **#

sync() {
  tmp=$(pwd)
  cd ../../rome/scripts
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321/alock --local_root=/Users/amandabaran/Desktop/sss/async_locks/alock --sync
  cd ${tmp}
  echo "Sync to Cloudlab Complete\n"
}

setup() {
  tmp=$(pwd)
  cd ../../rome/scripts
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321/alock --local_root=/Users/amandabaran/Desktop/sss/async_locks/alock --sync --cmd="cd alock/rome/scripts/setup && python3 run.py --resources all"
  cd ${tmp}
  echo "Setup on Cloudlab Complete\n"
}


# takes in a bazel build target to build on cloudlab (i.e. "--lock_type=alock //alock/benchmark/one_lock:main", "//alock/locks/a_lock:a_lock")
build() { 
  tmp=$(pwd)
  cd ../../rome/scripts
  array=("$@")
  build_str=${array[*]}
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321/alock --local_root=/Users/amandabaran/Desktop/sss/async_locks/alock --cmd="cd ~/alock/alock && ~/go/bin/bazelisk build -c opt $build_str"
  cd ${tmp}
  echo "Build Complete\n"
}

#** START OF SCRIPT **

sync

setup


if [ "$#" -eq  "0" ]
then
    echo "Done\n"
    exit 0
else
    echo "Building $1"
    to_build=$1
    build ${to_build}
fi

echo "Done\n"