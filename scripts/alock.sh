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
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321/alock --local_root=/Users/amandabaran/Desktop/sss/async_locks/alock --cmd="cd alock/alock && ~/go/bin/bazelisk build -c opt --action_env=BAZEL_CXXOPTS='-std=c++20' --lock_type=$1 //alock/benchmark/one_lock:main"
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

save_dir="test_local_p"

lock="alock"
log_level='info'
echo "Building ${lock}..."
build ${lock}

for num_nodes in 1
do
  for num_threads in 1
    do 
      for max in 10
      do
        for local_p in 1.0 0.5
        do 
          num_clients=$((num_threads * num_nodes))
          bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} -C ${num_clients} --nodes=${num_nodes} --threads=${num_threads} --budget=5 --max_key=${max} --p_local=${local_p} --lock_type=${lock} --ssh_user=adb321 --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --dry_run=False --gdb=False
        done
      done
    done
done

# for num_nodes in 2 4 8 12 16 20
# do 
#   for num_threads in 2 4 8 16 32 48
#   do 
#     for keys in 1 10 100 1000
#     do
#       num_clients=$((num_threads * num_nodes))  
#       bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} -C ${num_clients} --nodes=${num_nodes} --ssh_user=adb321 --lock_type=${lock} --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=${num_threads} --max_key=${keys} --budget=5
#     done
#   done
# done

# echo "Getting ${lock} data"
# bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile}  --ssh_user=adb321  --get_data  --local_save_dir=${workspace}/benchmark/one_lock/results/${save_dir}/ --remote_save_dir=/users/adb321/alock/alock/bazel-bin/alock/benchmark/one_lock/main.runfiles/alock/${save_dir} --lock_type=${lock}



# for num_nodes in 1
# do
#   for num_threads in 8 16 24 32 48
#   do
#     bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} --nodes=${num_nodes} --ssh_user=adb321 --lock_type=${lock} --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=${num_threads} --gdb=False --max_key=100 --dry_run=False
#   done
# done

# for num_nodes in 1 2 5 10 15 20
# do
#   for num_threads in  1 2 3 4 5 6 7 8 
#   # for num_threads in 50 100 150 200 250 300 350 400 450 500 550 600 650 700 750 800 850 900 950 1000
#     do 
#       for max in 10 100 1000 10000 
#       # for max in 100 1000 10000
#       do
#         bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} --nodes=${num_nodes} --ssh_user=adb321 --lock_type=${lock} --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=${num_threads} --gdb=False --max_key=${max} --dry_run=False
#       done
#     done
# done

# for num_nodes in 1
# do
#   for max in 10 20 50 100 1000 10000
#   do
#     bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} --nodes=${num_nodes} --ssh_user=adb321 --lock_type=${lock} --think_ns=0 --runtime=10 --remote_save_dir=${save_dir} --log_level=${log_level} --threads=80 --gdb=False --max_key=${max} --dry_run=False
#   done
# done