#!/bin/bash

workspace=/Users/amandabaran/Desktop/sss/async_locks/alock/alock/alock
nodefile=/Users/amandabaran/Desktop/sss/async_locks/alock/alock/alock/benchmark/nodefiles/luigi.csv

#** FUNCTION DEFINITIONS **#

sync() {
  tmp=$(pwd)
  cd ../../rome/scripts
  python rexec.py -n ${nodefile} --remote_user=amanda --remote_root=/home/amanda/alock --local_root=/Users/amandabaran/Desktop/sss/async_locks/alock --sync
  cd ${tmp}
  echo "Sync to Luigi Complete\n"
}

# takes in a bazel build target to build on luigi (i.e. "--lock_type=alock //alock/benchmark/one_lock:main", "//alock/locks/a_lock:a_lock")
build() { 
  tmp=$(pwd)
  cd ../../rome/scripts
  array=("$@")
  build_str=${array[*]}
  python rexec.py -n ${nodefile} --remote_user=amanda --remote_root=/home/amanda/alock --local_root=/Users/amandabaran/Desktop/sss/async_locks/alock --cmd="cd ~/alock/alock && ~/go/bin/bazelisk build -c opt --lock_type=$1 //alock/benchmark/one_lock:main --action_env=BAZEL_CXXOPTS='-std=c++20'"
  cd ${tmp}
  echo "Build Complete\n"
}

#** START OF SCRIPT **

sync

lock="spin"
log_level='trace'
echo "Building ${lock}..."
build ${lock}

save_dir="spintest"

for num_nodes in 2
do
  for num_threads in 1
  do
    bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} --nodes=${num_nodes} --ssh_user=amanda --lock_type=${lock} --think_ns=500 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=${num_threads} --gdb=True --max_key=10 --dry_run=False
  done
done

echo "Done\n"