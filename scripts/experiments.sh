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

save_dir="budget_test"

lock="alock"
log_level='debug'
echo "Building ${lock}..."
build ${lock}

for num_nodes in 2 5 10 20 
do
  for num_clients in 10 20 40 80 120 180
    do 
      for max in 1 10
      do
        for budget in 2 5 10 20 30 50 75 100
        do 
          num_threads=$(expr $num_clients / $num_nodes)
          bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} -C ${num_clients} --nodes=${num_nodes} --ssh_user=adb321 --lock_type=${lock} --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=${num_threads} --budget=${budget} --max_key=${max} --dry_run=False
        done
      done
    done
done

# save_dir="locality_test"

# lock="alock"
# log_level='info'
# echo "Building ${lock}..."
# build ${lock}

# for num_nodes in 1 2 5 10 20 
# do
#   for num_threads in 8
#     do 
#       for max in 10
#       do
#         for local_p in 100 75 50 25
#         do 
#           num_clients=$((num_threads * num_nodes))
#           bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} -C ${num_clients} --nodes=${num_nodes} --ssh_user=adb321 --lock_type=${lock} --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=${num_threads} --budget=${budget} --max_key=${max} --dry_run=False
#         done
#       done
#     done
# done

echo "Getting ${lock} data"
zsh get_data.sh ${save_dir}
bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile}  --ssh_user=adb321  --get_data  --local_save_dir=${workspace}/benchmark/one_lock/results/${save_dir}/ --remote_save_dir=/users/adb321/alock/alock/bazel-bin/alock/benchmark/one_lock/main.runfiles/alock/${save_dir} --lock_type=${lock}
