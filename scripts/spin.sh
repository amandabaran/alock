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
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321/alock --local_root=/Users/amandabaran/Desktop/sss/async_locks/alock --cmd="cd alock/alock && ~/go/bin/bazelisk clean --expunge_async"
  cd ${tmp}
  echo "Clean Complete\n"
}

build() {
  tmp=$(pwd)
  cd ../../rome/scripts
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321/alock --local_root=/Users/amandabaran/Desktop/sss/async_locks/alock --cmd="cd alock/alock && ~/go/bin/bazelisk build --action_env=BAZEL_CXXOPTS='-std=c++20' -c opt --lock_type=$1 //alock/benchmark/one_lock:main"
  echo "Build Complete\n"
  cd ${tmp}
}


#** START OF SCRIPT **#

echo "Pushing local repo to remote nodes..."
sync

clean

lock="spin"
log_level='info'
echo "Building ${lock}..."
build ${lock}

save_dir="spin_test"

for num_nodes in 1
do 
  for num_threads in 148 156 164 172 184 196 512
  do 
    for max in 1000
    do
      num_clients=$((num_threads * num_nodes))  
      bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} -C ${num_clients} --nodes=${num_nodes} --ssh_user=adb321 --lock_type=${lock} --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=${num_threads} --local_budget=5 --remote_budget=20 --max_key=${max} --dry_run=False --p_local=1.0
    done
  done
done
