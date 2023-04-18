#!/bin/bash

workspace=/home/amanda/alock/alock/alock
nodefile=/home/amanda/alock/alock/alock/benchmark/nodefiles/xl170.csv

#** FUNCTION DEFINITIONS **#

sync_nodes() {
  tmp=$(pwd)
  cd ../rome/scripts
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321/alock --local_root=/home/amanda/alock --sync
  cd ${tmp}
  echo "Sync Complete\n"
}

clean() {
  tmp=$(pwd)
  cd ../rome/scripts
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321 --local_root=/home/amanda --cmd="cd alock/alock && ~/go/bin/bazelisk clean"
  cd ${tmp}
  echo "Clean Complete\n"
}


build_lock() {
  tmp=$(pwd)
  cd ../rome/scripts
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321 --local_root=/home/amanda --cmd="cd alock/alock && ~/go/bin/bazelisk build --verbose_failures -c opt --lock_type=$1 //alock/benchmark/one_lock:main" 
  cd ${tmp}
  echo "Build Complete\n"
}

#** START OF SCRIPT **

echo "Pushing local repo to remote nodes..."
sync_nodes

echo "Cleaning..."
clean

echo "Running Experiment"
save_dir="test_alock_node"
lock='alock'
log_level='debug'
echo "Building ${lock}..."
build_lock ${lock}

bazel run //alock/benchmark/one_lock:launch --lock_type=${lock} -- -n ${nodefile}  --ssh_user=adb321 -n 2 --think_ns=0 --runtime=5 --remote_save_dir=${save_dir} --log_level=${log_level} --lock_type=${lock} --gdb=False