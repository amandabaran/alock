#!/bin/env bash

source "exp.conf"

user=${ssh_user}
machines=("apt150" "apt138" "apt164" "apt148" "apt149" "apt140" "apt152" "apt142" "apt147" "apt158")
domain="apt.emulab.net"

path="results/write/rand/t${thread_count}/"

mkdir -p ${path}
cd ${path}

for m in ${machines[@]}; do
    mkdir $m
    scp "${user}@$m.$domain":~/tput_result.csv "./$m/"
    scp "${user}@$m.$domain":~/lat_result.csv "./$m/"
done
