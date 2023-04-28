#!/bin/bash

workspace=/home/amanda/alock/alock/alock
nodefile=~/alock/alock/alock/benchmark/nodefiles/xl170.csv

#** FUNCTION DEFINITIONS **#

setup() {
  tmp=$(pwd)
  cd ../../rome/scripts
  python rexec.py -n ${nodefile} --remote_user=adb321 --remote_root=/users/adb321/alock --local_root=/home/amanda/alock --sync --cmd="cd alock/rome/scripts/setup && python3 run.py --resources all"
  cd ${tmp}
  echo "Setup Complete\n"

#** START OF SCRIPT **

setup