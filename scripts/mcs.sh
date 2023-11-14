#!/bin/bash

# Source cluster-dependent variables
source "config2.conf"

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

build() {
  tmp=$(pwd)
  cd ../../rome/scripts
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321/alock --local_root=/Users/amandabaran/Desktop/sss/async_locks/alock --cmd="cd alock/alock && ~/go/bin/bazelisk build -c opt --lock_type=$1 //alock/benchmark/one_lock:main --action_env=BAZEL_CXXOPTS='-std=c++20'"
  echo "Build Complete\n"
  cd ${tmp}
}


#** START OF SCRIPT **#

echo "Pushing local repo to remote nodes..."
sync

clean

lock="mcs"
log_level='info'
echo "Building ${lock}..."
build ${lock}

save_dir="mcstest"


for num_nodes in 1 2 5 10 15 20
do
  for num_threads in  1 2 3 4 5 6 7 8 
  # for num_threads in 50 100 150 200 250 300 350 400 450 500 550 600 650 700 750 800 850 900 950 1000
    do 
      for max in 10 100 1000 10000 
      # for max in 100 1000 10000
      do
        num_clients=$((num_threads * num_nodes))
        bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} -C ${num_clients} --nodes=${num_nodes} --ssh_user=adb321 --lock_type=${lock} --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=${num_threads} --gdb=False --max_key=${max} --dry_run=False
      done
    done
done