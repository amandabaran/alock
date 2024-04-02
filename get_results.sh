#!/bin/env bash

source "exp.conf"

user=${ssh_user}
machines=("apt150" "apt138" "apt164" "apt148" "apt149" "apt140" "apt152" "apt142" "apt147" "apt158")
domain="apt.emulab.net"

mkdir -p results/top/t2/
cd results/top/t2/

for m in ${machines[@]}; do
    mkdir $m
    scp "${user}@$m.$domain":~/exp_result.csv "./$m/"
done
