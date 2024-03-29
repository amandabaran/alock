#!/bin/env bash

source "exp.conf"

user=${ssh_user}
machines=("apt076" "apt075" "apt081" "apt164" "apt141" "apt158" "apt148" "apt163" "apt153" "apt145")
domain="apt.emulab.net"

mkdir results
cd results

for m in ${machines[@]}; do
    mkdir $m
    scp "${user}@$m.$domain":~/exp_result.csv "./$m/"
done
