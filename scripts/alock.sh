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
  # if [ $? -ne 0 ]; then 
  #   echo "Build Error: ${result}" 
  #   exit 1
  # fi
  echo "Build Complete\n"
  cd ${tmp}
}


#** START OF SCRIPT **#

echo "Pushing local repo to remote nodes..."
sync

clean


save_dir="20keys"
lock="alock"
log_level='info'

echo "Building ${lock}..."
build ${lock}

for num_nodes in 5 10 20
do
  for num_threads in 4 6 8 10 12
  do 
    for max in 20
    do
      for local_p in 1 .95 .9 .85
      do 
        num_clients=$(( num_threads*num_nodes ))
        bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} -C ${num_clients} --nodes=${num_nodes} --ssh_user=adb321 --lock_type=${lock} --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=${num_threads} --local_budget=${local_budget} --remote_budget=${remote_budget} --max_key=${max} --dry_run=False --p_local=${p_local}
      done
    done
  done
done