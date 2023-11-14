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

build() {
  tmp=$(pwd)
  cd ../../rome/scripts
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321/alock --local_root=/Users/amandabaran/Desktop/sss/async_locks/alock --cmd="cd alock/alock && ~/go/bin/bazelisk build -c opt --lock_type=$1 //alock/benchmark/one_lock:main --action_env=BAZEL_CXXOPTS='-std=c++20'"
  # if [ $? -ne 0 ]; then 
  #   echo "Build Error: ${result}" 
  #   exit 1
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
log_level='info'
echo "Building ${lock}..."
build ${lock}

# 1000 keys, clients up to 128, nodes up to 5
save_dir="k1000_c1_128_n1_5"

for num_nodes in 1 2 3 4 5 
do
  for num_threads in 1 2 4 8 16 32 64 128
    do 
      for max in 1000
      do
        bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} --nodes=${num_nodes} --ssh_user=adb321 --lock_type=${lock} --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=${num_threads} --gdb=False --max_key=${max} --dry_run=False
      done
    done
done
# get data
bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile}  --ssh_user=adb321  --get_data  --local_save_dir=${workspace}/benchmark/one_lock/results/${save_dir}/ --remote_save_dir=/users/adb321/alock/alock/bazel-bin/alock/benchmark/one_lock/main.runfiles/alock/${save_dir} --lock_type=${lock}

save_dir="k1_10000_c10_180_n1"
for num_nodes in 1
do
  for num_threads in 10 50 75 100 125 150 180
    do 
      for max in 10000 5000 2500 1000 750 500 250 200 180 150 125 100 90 80 70 60 50 40 30 20 10 5 1
      do
        bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} --nodes=${num_nodes} --ssh_user=adb321 --lock_type=${lock} --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=${num_threads} --gdb=False --max_key=${max} --dry_run=False
      done
    done
done
# get data
bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile}  --ssh_user=adb321  --get_data  --local_save_dir=${workspace}/benchmark/one_lock/results/${save_dir}/ --remote_save_dir=/users/adb321/alock/alock/bazel-bin/alock/benchmark/one_lock/main.runfiles/alock/${save_dir} --lock_type=${lock}


lock="spin"
log_level='info'
echo "Building ${lock}..."
build ${lock}

# 1000 keys, clients up to 180, nodes up to 5
save_dir="k1000_c180_n5"

for num_nodes in 1 2 3 4 5 
do
  t_nums=()
  for ((num = 1; num < 180; num++)); do
      if [ $((num % num_nodes)) -eq 0 ]; then
          t_nums+=($num)
      fi
  done
  for num_threads in t_nums
    echo "\n\nNum Threads is ${num_threads}"
    do 
      for max in 1000
      do
        bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} --nodes=${num_nodes} --ssh_user=adb321 --lock_type=${lock} --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=${num_threads} --gdb=False --max_key=${max} --dry_run=False
      done
    done
done
# get data
bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile}  --ssh_user=adb321  --get_data  --local_save_dir=${workspace}/benchmark/one_lock/results/${save_dir}/ --remote_save_dir=/users/adb321/alock/alock/bazel-bin/alock/benchmark/one_lock/main.runfiles/alock/${save_dir} --lock_type=${lock}

exit 0

save_dir="k1_10000_c10_180_n1"
for num_nodes in 1
do
  for num_threads in 10 50 75 100 125 150 180
    do 
      for max in 10000 5000 2500 1000 750 500 250 200 180 150 125 100 90 80 70 60 50 40 30 20 10 5 1
      do
        bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} --nodes=${num_nodes} --ssh_user=adb321 --lock_type=${lock} --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=${num_threads} --gdb=False --max_key=${max} --dry_run=False
      done
    done
done
# get data
bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile}  --ssh_user=adb321  --get_data  --local_save_dir=${workspace}/benchmark/one_lock/results/${save_dir}/ --remote_save_dir=/users/adb321/alock/alock/bazel-bin/alock/benchmark/one_lock/main.runfiles/alock/${save_dir} --lock_type=${lock}

