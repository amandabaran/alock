#!/bin/bash

workspace=/home/amanda/alock/alock/alock
nodefile=~/alock/alock/alock/benchmark/nodefiles/xl170.csv

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
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321 --local_root=/home/amanda --cmd="cd alock/alock && ~/go/bin/bazelisk build -c opt --lock_type=$1 //alock/benchmark/baseline:main"
  cd ${tmp}
  echo "Build Complete\n"
}

#** START OF SCRIPT **#

echo "Cleaning..."
# clean

# echo "Pushing local repo to remote nodes..."
sync_nodes

#  LOCAL WORKLOAD PERFORMANCE
# echo "Running Experiment #1"
# lock='mcs'
# log_level='info'
# echo "Building ${lock}..."
# build_lock ${lock}

# echo "Running..."
# save_dir="exp1_mcs"
# for num_clients in 2
# do
#   bazel run //alock/benchmark/baseline:launch --lock_type=${lock} -- -n ${nodefile}  --ssh_user=adb321 -c ${num_clients} -s 1 --think_ns=500 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --lock_type=${lock}
# done
# bazel run //alock/benchmark/baseline:launch --lock_type=${lock} -- -n ${nodefile}  --ssh_user=adb321  --get_data  --local_save_dir=${workspace}/benchmark/baseline/results/${save_dir}/ --remote_save_dir=${save_dir} --lock_type=${lock}



echo "Running Experiment #2"
save_dir="test_alock"
lock='alock'
log_level='info'
echo "Building ${lock}..."
# build_lock ${lock}

bazel run //alock/benchmark/baseline:launch --lock_type=${lock} -- -n ${nodefile}  --ssh_user=adb321 -c 2 -s 1 --think_ns=0 --runtime=2 --remote_save_dir=${save_dir} --log_level=${log_level} --lock_type=${lock} --gdb=True

# echo "Running..."
# save_dir="exp3_alock"
# for num_clients in 2 3 4 5 6 7 8 9 
# do
#   bazel run //alock/benchmark/baseline:launch --lock_type=${lock} -- -n ${nodefile}  --ssh_user=adb321 -c ${num_clients} -s 1 --think_ns=500 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --lock_type=${lock}
# done
# bazel run //alock/benchmark/baseline:launch --lock_type=${lock} -- -n ${nodefile}  --ssh_user=adb321  --get_data  --local_save_dir=${workspace}/benchmark/baseline/results/${save_dir}/ --remote_save_dir=${save_dir} --lock_type=${lock}
