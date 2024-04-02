#!/bin/env bash

source "exp.conf"

remus_dir="../remus-internal"
exe="cloudlab_depend.sh"

user=${ssh_user}
machines=("apt150" "apt138" "apt164" "apt148" "apt149" "apt140" "apt152" "apt142" "apt147" "apt158")
domain="apt.emulab.net"

for m in ${machines[@]}; do
  ssh "${user}@$m.$domain" hostname
done

# Send install script 
for m in ${machines[@]}; do
  scp -r "${remus_dir}/${exe}" "${user}@$m.$domain":~
done

# run install script
for m in ${machines[@]}; do
  ssh "${user}@$m.$domain" bash ${exe}
done