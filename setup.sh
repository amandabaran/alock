#!/bin/env bash

source "exp.conf"

remus_dir="../remus-internal"
exe="cloudlab_depend.sh"

user=${ssh_user}
machines=("apt104" "apt120" "apt108" "apt103" "apt101" "apt097" "apt111" "apt105" "apt106" "apt113")
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