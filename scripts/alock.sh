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


save_dir="keys10k"
lock="alock"
log_level='info'

echo "Building ${lock}..."
build ${lock}

for num_nodes in 5
do
  for num_clients in 120
    do 
      for max in 10000
      do
        for local_p in .6 .5
        do 
          num_threads=$((num_clients / num_nodes))
          bazel run --action_env=BAZEL_CXXOPTS='-std=c++20' //alock/benchmark/one_lock:launch -- -n ${nodefile} -C ${num_clients} --nodes=${num_nodes} --threads=${num_threads} --max_key=${max} --p_local=${local_p} --lock_type=${lock} --ssh_user=adb321 --think_ns=0 --runtime=5 --remote_save_dir=${save_dir} --log_level=${log_level} --dry_run=False --gdb=False
        done
      done
    done
done