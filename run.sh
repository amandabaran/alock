#!/bin/env bash

source "exp.conf"

DEBUG=0

exe_dir="./build"
exe="main"

cd build
make -j ${exe}
cd ..

user=${ssh_user}
machines=("apt076" "apt075" "apt081")
domain="apt.emulab.net"
node_list="node0,node1,node2"

for m in ${machines[@]}; do
  ssh "${user}@$m.$domain" hostname
done

for m in ${machines[@]}; do
  ssh "${user}@$m.$domain" pkill -9 "${exe}"
  scp "${exe_dir}/${exe}" "${user}@$m.$domain":~
done

echo "#!/bin/env bash" > exp_run.sh

for m in ${machines[@]}; do
  echo "ssh ${user}@$m.$domain pkill -9 ${exe}" >> exp_run.sh
done

echo -n "tmux new-session \\; " >> exp_run.sh
idx=0
for m in ${machines[@]}; do
  echo " \\" >> exp_run.sh
  if [[ $idx -ne 0 ]]; then
    echo " new-window \; \\" >> exp_run.sh
  fi
  echo -n " send-keys 'ssh ${user}@$m.$domain ./${exe} --node_count ${num_nodes} --node_id ${idx} --runtime ${runtime} --op_count ${op_count} --min_key ${min_key} --max_key ${max_key} --region_size ${region_size} --thread_count ${thread_count} --qp_max ${qp_max} --p_local ${p_local} --local_budget ${local_budget} --remote_budget ${remote_budget}' C-m \\; " >> exp_run.sh
  idx=$((idx + 1))
done

chmod +x exp_run.sh

./exp_run.sh
