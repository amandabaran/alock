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


save_dir="nodescale"
lock="alock"
log_level='info'

echo "Building ${lock}..."
build ${lock}

for num_nodes in 5 10 15 20
do
  for num_clients in 40 80 120 180
    do 
      for max in 100 1000 10000
      do
        for local_p in 1 .95 .9 .8 .7 .6
        do 
          num_threads=$((num_clients / num_nodes))
          bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} -C ${num_clients} --nodes=${num_nodes} --threads=${num_threads} --budget=5 --max_key=${max} --p_local=${local_p} --lock_type=${lock} --ssh_user=adb321 --think_ns=0 --runtime=5 --remote_save_dir=${save_dir} --log_level=${log_level} --dry_run=False --gdb=False
        done
      done
    done
done

echo "Getting ${lock} data"
zsh get_data.sh ${save_dir}

lock="spin"

echo "Building ${lock}..."
build ${lock}

for num_nodes in 5 10 15 20
do
  for num_clients in 40 80 120 180
    do 
      for max in 100 1000 10000
      do
        for local_p in 1 .95 .9 .8 .7 .6
        do 
          num_threads=$((num_clients / num_nodes))
          bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} -C ${num_clients} --nodes=${num_nodes} --threads=${num_threads} --budget=5 --max_key=${max} --p_local=${local_p} --lock_type=${lock} --ssh_user=adb321 --think_ns=0 --runtime=5 --remote_save_dir=${save_dir} --log_level=${log_level} --dry_run=False --gdb=False
        done
      done
    done
done

echo "Getting ${lock} data"
zsh get_data.sh ${save_dir}

lock="mcs"

echo "Building ${lock}..."
build ${lock}

for num_nodes in 5 10 15 20
do
  for num_clients in 40 80 120 180
    do 
      for max in 100 1000 10000
      do
        for local_p in 1 .95 .9 .8 .7 .6
        do 
          num_threads=$((num_clients / num_nodes))
          bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} -C ${num_clients} --nodes=${num_nodes} --threads=${num_threads} --budget=5 --max_key=${max} --p_local=${local_p} --lock_type=${lock} --ssh_user=adb321 --think_ns=0 --runtime=5 --remote_save_dir=${save_dir} --log_level=${log_level} --dry_run=False --gdb=False
        done
      done
    done
done


# save_dir="sanity_check"

# lock="spin"
# log_level='info'

# echo "Building ${lock}..."
# build ${lock}

# for num_nodes in 5
# do
#   for num_clients in 40 80
#     do 
#       for max in 100 1000
#       do
#         for local_p in 1 .95 .9 .8 .7 .6 .5 .4 .3 .2 .1 .05 0
#         do 
#           num_threads=$((num_clients / num_nodes))
#           bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} -C ${num_clients} --nodes=${num_nodes} --threads=${num_threads} --budget=5 --max_key=${max} --p_local=${local_p} --lock_type=${lock} --ssh_user=adb321 --think_ns=0 --runtime=5 --remote_save_dir=${save_dir} --log_level=${log_level} --dry_run=False --gdb=False
#         done
#       done
#     done
# done

# echo "Getting ${lock} data"
# zsh get_data.sh ${save_dir}

# lock="mcs"
# log_level='info'

# echo "Building ${lock}..."
# build ${lock}

# for num_nodes in 5
# do
#   for num_clients in 10 40 80
#     do 
#       for max in 100 1000
#       do
#         for local_p in 1 .95 .9 .8 .7 .6 .5 .4 .3 .2 .1 .05 0
#         do 
#           num_threads=$((num_clients / num_nodes))
#           bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} -C ${num_clients} --nodes=${num_nodes} --threads=${num_threads} --budget=5 --max_key=${max} --p_local=${local_p} --lock_type=${lock} --ssh_user=adb321 --think_ns=0 --runtime=5 --remote_save_dir=${save_dir} --log_level=${log_level} --dry_run=False --gdb=False
#         done
#       done
#     done
# done

# echo "Getting ${lock} data"
# zsh get_data.sh ${save_dir}

# save_dir="xortest_local_p"
# lock="alock"
# log_level='info'

# echo "Building ${lock}..."
# build ${lock}

# for num_nodes in 5 10
# do
#   for num_clients in 40 80 160
#     do 
#       for max in 10 100
#       do
#         for local_p in 1.0 0.99 0.9 0.8 0.7 0.6 0.5 0.4 0.3 0.2 0.15 0.1 0.05 0.01
#         do 
#           num_threads=$((num_clients / num_nodes))
#           bazel run //alock/benchmark/one_lock:launch -- -n ${nodefile} -C ${num_clients} --nodes=${num_nodes} --threads=${num_threads} --budget=5 --max_key=${max} --p_local=${local_p} --lock_type=${lock} --ssh_user=adb321 --think_ns=0 --runtime=5 --remote_save_dir=${save_dir} --log_level=${log_level} --dry_run=False --gdb=False
#         done
#       done
#     done
# done