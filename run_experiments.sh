#!/bin/bash

workspace=/home/amanda/alock/alock/alock
# nodefile=~/alock/alock/alock/benchmark/nodefiles/d6515.csv
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
clean

echo "Pushing local repo to remote nodes..."
sync_nodes

#  LOCAL WORKLOAD PERFORMANCE
echo "Running Experiment #1: Spin Lock vs MCS vs A-Lock, 1 lock on 1 server"
lock='spin'
log_level='info'
echo "Building ${lock}..."
build_lock ${lock}

echo "Running..."
save_dir="exp1"
for num_clients in 10 15 20 25 30 35 40 45 50 55 60 65 70
do
  bazel run //alock/benchmark/baseline:launch --lock_type=${lock} -- -n ${nodefile}  --ssh_user=adb321 -c ${num_clients} -s 1 --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --lock_type=${lock}
done
bazel run //alock/benchmark/baseline:launch --lock_type=${lock} -- -n ${nodefile}  --ssh_user=adb321  --get_data  --local_save_dir=${workspace}/benchmark/baseline/results/${save_dir}/ --remote_save_dir=${save_dir} --lock_type=${lock}

echo "Cleaning..."
clean

lock="mcs"
log_level='info'
echo "Building ${lock}..."
build_lock ${lock}

echo "Running..."
save_dir="exp1"
for num_clients in 10 15 20 25 30 35 40 45 50 55 60 65 70 
do
  bazel run //alock/benchmark/baseline:launch --lock_type=${lock} -- -n ${nodefile}  --ssh_user=adb321 -c ${num_clients} -s 1 --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --lock_type=${lock}
done
bazel run //alock/benchmark/baseline:launch --lock_type=${lock} -- -n ${nodefile}  --ssh_user=adb321  --get_data  --local_save_dir=${workspace}/benchmark/baseline/results/${save_dir}/ --remote_save_dir=${save_dir} --lock_type=${lock}


# echo "Cleaning..."
# clean

# lock="a_lock"
# log_level='debug'
# echo "Building ${lock}..."
# build_lock ${lock}

# save_dir="exp1"
# # for num_clients in 10 15 20 25 30 35 40 45 50 55 60 65 70
# for num_clients in 10
# do
#   bazel run //alock/benchmark/baseline:launch --lock_type=${lock} -- -n ${nodefile}  --ssh_user=adb321 -c ${num_clients} -s 1 --think_ns=500 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --lock_type=${lock}
# done
# bazel run //alock/benchmark/baseline:launch --lock_type=${lock} -- -n ${nodefile}  --ssh_user=adb321  --get_data  --local_save_dir=${workspace}/benchmark/baseline/results/${save_dir}/ --remote_save_dir=${save_dir} --lock_type=${lock}

