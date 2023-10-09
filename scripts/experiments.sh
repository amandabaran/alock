#!/bin/bash

# Source cluster-dependent variables
source "config.conf"

#** FUNCTION DEFINITIONS **#

sync() {
  tmp=$(pwd)
  cd ../../rome/scripts
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321/alock --local_root=/Users/amandabaran/Desktop/sss/async_locks/alock --sync
  cd ${tmp}
  echo "Sync to Cloudlab Complete\n"
}

clean() {
  tmp=$(pwd)
  cd ../../rome/scripts
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321/alock --local_root=/Users/amandabaran/Desktop/sss/async_locks/alock --cmd="cd alock/alock && ~/go/bin/bazelisk clean"
  cd ${tmp}
  echo "Clean Complete\n"
}

command() {
  tmp=$(pwd)
  cd ../../rome/scripts
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321/alock --local_root=/Users/amandabaran/Desktop/sss/async_locks/alock --cmd="$1"
  cd ${tmp}
  echo "Clean Complete\n"
}

build() {
  tmp=$(pwd)
  cd ../../rome/scripts
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321/alock --local_root=/Users/amandabaran/Desktop/sss/async_locks/alock --cmd="cd alock/alock && ~/go/bin/bazelisk build -c opt --lock_type=$1 //alock/benchmark/one_lock:main --action_env=BAZEL_CXXOPTS='-std=c++20'"
  # if [[ $(ls -A) ]] 
  # then 
  #   echo "Build Error. See Logs." 
  #   exit
  # fi
  echo "Build Complete\n"
  cd ${tmp}
}


#** START OF SCRIPT **#

echo "Cleaning..."
clean

echo "Pushing local repo to remote nodes..."
sync

# command

lock="alock"
log_level='debug'
echo "Building ${lock}..."
build ${lock}

# save_dir="exp2"

# for num_nodes in 2 3 4 5
# do
#   bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} --ssh_user=adb321 -N ${num_nodes} --lock_type=${lock} --think_ns=500 --runtime=5 --remote_save_dir=${save_dir} --log_level=${log_level} --dry_run=False --gdb=False
# done
# bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile}  --ssh_user=adb321 --lock_type=${lock} --get_data  --local_save_dir=${workspace}/benchmark/one_lock/results/${save_dir}/ --remote_save_dir=${save_dir}

save_dir="exp11"

for num_nodes in 2
do
  # for num_threads in 2 4 6 8 10 15 20 25 30 35 40 45 50 55 60 65 70 75 80 85 90
  for num_threads in 1
  do
    bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} --nodes=${num_nodes} --ssh_user=adb321 --lock_type=${lock} --think_ns=500 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=${num_threads} --gdb=False --max_key=10000 --dry_run=False --port=12100
  done
done
# bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} --ssh_user=adb321 --lock_type=${lock} --get_data  --local_save_dir=${workspace}/benchmark/one_lock/results/${save_dir}/ --remote_save_dir=${save_dir}

#  LOCAL WORKLOAD PERFORMANCE
# echo "Running Experiment #1: Spin Lock vs MCS vs A-Lock, 1 lock on 1 server"
# lock='spin'
# log_level='info'
# echo "Building ${lock}..."
# build_lock ${lock}

# echo "Running..."
# save_dir="exp1"
# for num_clients in 10 15 20 25 30 35 40 45 50 55 60 65 70
# do
#   bazel run //alock/benchmark/baseline:launch --lock_type=${lock} -- -n ${nodefile}  --ssh_user=adb321 -c ${num_clients} -s 1 --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --lock_type=${lock}
# done
# bazel run //alock/benchmark/baseline:launch --lock_type=${lock} -- -n ${nodefile}  --ssh_user=adb321  --get_data  --local_save_dir=${workspace}/benchmark/baseline/results/${save_dir}/ --remote_save_dir=${save_dir} --lock_type=${lock}

# echo "Cleaning..."
# clean

# lock="mcs"
# log_level='info'
# echo "Building ${lock}..."
# build_lock ${lock}

# echo "Running..."
# save_dir="exp1"
# for num_clients in 10 15 20 25 30 35 40 45 50 55 60 65 70 
# do
#   bazel run //alock/benchmark/baseline:launch --lock_type=${lock} -- -n ${nodefile}  --ssh_user=adb321 -c ${num_clients} -s 1 --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --lock_type=${lock}
# done
# bazel run //alock/benchmark/baseline:launch --lock_type=${lock} -- -n ${nodefile}  --ssh_user=adb321  --get_data  --local_save_dir=${workspace}/benchmark/baseline/results/${save_dir}/ --remote_save_dir=${save_dir} --lock_type=${lock}


