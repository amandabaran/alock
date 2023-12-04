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

save_dir="alock_test"

# lock="alock"
# log_level='info'
# echo "Building ${lock}..."
# build ${lock}

# for num_nodes in 10
# do
#   for num_threads in 8 12 16 20
#     do 
#       for max in 1 10 100 1000
#       do
#         num_clients=$((num_threads * num_nodes))
#         bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} -C ${num_clients} --nodes=${num_nodes} --ssh_user=adb321 --lock_type=${lock} --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=${num_threads} --gdb=False --max_key=${max} --dry_run=False
#       done
#     done
# done

# echo "Getting ${lock} data"
# zsh get_data.sh ${save_dir}
# bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile}  --ssh_user=adb321  --get_data  --local_save_dir=${workspace}/benchmark/one_lock/results/${save_dir}/ --remote_save_dir=/users/adb321/alock/alock/bazel-bin/alock/benchmark/one_lock/main.runfiles/alock/${save_dir} --lock_type=${lock}

# echo "Killing bazel processes"
# zsh shutdown.sh

# lock="spin"
# log_level='info'
# echo "Building ${lock}..."
# build ${lock}

# for num_nodes in 1 2 5 10
# do
#   for num_threads in  1 2 4 8 12 16 20
#     do 
#       for max in 1 10 100 1000
#       do
#         num_clients=$((num_threads * num_nodes))
#         bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} -C ${num_clients} --nodes=${num_nodes} --ssh_user=adb321 --lock_type=${lock} --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=${num_threads} --gdb=False --max_key=${max} --dry_run=False
#       done
#     done
# done

# echo "Getting ${lock} data"
# zsh get_data.sh ${save_dir}

lock="mcs"
log_level='info'
echo "Building ${lock}..."
build ${lock}

for num_nodes in 2 5 10
do
  for num_threads in  1 2 4 8 12 16 20
    do 
      for max in 1 10 100 1000
      do
        num_clients=$((num_threads * num_nodes))
        bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} -C ${num_clients} --nodes=${num_nodes} --ssh_user=adb321 --lock_type=${lock} --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=${num_threads} --gdb=False --max_key=${max} --dry_run=False
      done
    done
done

echo "Getting ${lock} data"
zsh get_data.sh ${save_dir}


# # 2 Nodes
# num_nodes=2
# for num_threads in 10 20 30 40 50 60 70 80 90
# do 
#   for max in 1000 750 500 250 100 50 25 10
#   do
#     num_clients=$((num_threads * num_nodes))  
#     bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} -C ${num_clients} --nodes=${num_nodes} --ssh_user=adb321 --lock_type=${lock} --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=${num_threads} --gdb=False --max_key=${max} --dry_run=False
#   done
# done

# # get data
# bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile}  --ssh_user=adb321  --get_data  --local_save_dir=${workspace}/benchmark/one_lock/results/${save_dir}/ --remote_save_dir=/users/adb321/alock/alock/bazel-bin/alock/benchmark/one_lock/main.runfiles/alock/${save_dir} --lock_type=${lock}

# # 3 Nodes
# num_nodes=3
# for num_threads in 10 20 30 40 50 60
# do 
#   for max in 1000 750 500 250 100 50 25 10
#   do
#     num_clients=$((num_threads * num_nodes))
#     bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} -C ${num_clients} --nodes=${num_nodes} --ssh_user=adb321 --lock_type=${lock} --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=${num_threads} --gdb=False --max_key=${max} --dry_run=False
#   done
# done

# # get data
# bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile}  --ssh_user=adb321  --get_data  --local_save_dir=${workspace}/benchmark/one_lock/results/${save_dir}/ --remote_save_dir=/users/adb321/alock/alock/bazel-bin/alock/benchmark/one_lock/main.runfiles/alock/${save_dir} --lock_type=${lock}

# # 4 Nodes
# num_nodes=4
# for num_threads in 10 20 30 40 45
# do 
#   for max in 1000 750 500 250 100 50 25 10
#   do
#     num_clients=$((num_threads * num_nodes))
#     bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} -C ${num_clients} --nodes=${num_nodes} --ssh_user=adb321 --lock_type=${lock} --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=${num_threads} --gdb=False --max_key=${max} --dry_run=False
#   done
# done

# # get data
# bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile}  --ssh_user=adb321  --get_data  --local_save_dir=${workspace}/benchmark/one_lock/results/${save_dir}/ --remote_save_dir=/users/adb321/alock/alock/bazel-bin/alock/benchmark/one_lock/main.runfiles/alock/${save_dir} --lock_type=${lock}

# # 5 Nodes
# num_nodes=5
# for num_threads in 10 20 30 36
# do 
#   for max in 1000 750 500 250 100 50 25 10
#   do
#     num_clients=$((num_threads * num_nodes))
#     bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} -C ${num_clients} --nodes=${num_nodes} --ssh_user=adb321 --lock_type=${lock} --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=${num_threads} --gdb=False --max_key=${max} --dry_run=False
#   done
# done

# # get data
# bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile}  --ssh_user=adb321  --get_data  --local_save_dir=${workspace}/benchmark/one_lock/results/${save_dir}/ --remote_save_dir=/users/adb321/alock/alock/bazel-bin/alock/benchmark/one_lock/main.runfiles/alock/${save_dir} --lock_type=${lock}