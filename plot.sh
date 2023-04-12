#!/bin/bash

workspace=/home/amanda/alock/alock/alock
nodefile=~/alock/alock/alock/benchmark/nodefiles/xl170.csv


save_dir="exp1"
echo "Plotting results in ${save_dir}"

bazel run //alock/benchmark/baseline:launch -- -n ${nodefile}  --ssh_user=adb321  --plot  --local_save_dir=${workspace}/benchmark/baseline/results/${save_dir}/ --remote_save_dir=${save_dir}
