# ALock README


# Setup

First, in a directory containing this alock repo, downlaod the rome repo using the 'sss-tutorials' branch. https://github.com/amandabaran/rome.git

In order to run the experiments, you first need to create a CloudLab experiment with a cluster of 20 r320 nodes.
Copy the node information in csv format into the nodefile, found in alock/benchmark/nodefiles/r320.csv.

From alock/scripts, you can run the cloudlab_setup.sh script. This will install all necessary packages and dependencies needed on the nodes that are in the r320.csv nodefile.


# Experiments

In order to recreate Figure 1, the study showing the effects of loopback traffic using a spinlock, you can run the spin_exp.sh script. 

To recreate Figure 4, the study showing the effects of the remote and local budget, you can run the budget_exp.sh script.

To recreate Figures 5 and 6, the plots showing the throughput and latency of the alock and its competitiors, you can run the scalability_exp.sh script. 

# Plots

In order to plot the data, run the plot.sh script with the name of the directory to be plotted.

For example, plotting the scalability experiment would be the command: 
"zsh plot.sh scalability_exp"

# POST SUBMISSION STUFF BELOW: 

# Commands

## Build Dependecies

<!-- rebuilds and installs remus into /opt/ -->
cd remus/tools
sh install.sh 

## Change Experiment Configuration
cd alock
Edit "exp.conf"

## Build ALock Executable

Create build directory. 
``mkdir build``
``cd build``

Generate build system. 
``cmake -DCMAKE_PREFIX_PATH=/opt/remus/lib/cmake -DCMAKE_MODULE_PATH=/opt/remus/lib/cmake ..``

Build. 
``make -j``


### Log Level Flag:
-DLOG_LEVEL=INFO

### CMake Build Type
-DCMAKE_BUILD_TYPE=Release (-o3)
-DCMAKE_BUILD_TYPE=Debug (-g) 
-DCMAKE_BUILD_TYPE=RelWithDebInfo (-o3 and -g)


## Send to Clouldab and Run

<!-- Builds executable, sends to nodes, and runs exp_run.sh  -->
cd alock
bash run.sh  <!-- TODO: Update with cloudlab node info  -->


# Old Commands: 

## Sync
sh sync.sh -u adb321

## Install Dependencies 
From remus/scripts:
<!-- -i installs dependencies for first time connecting to nodes -->
sh sync.sh -u adb321 -i 

## Run 
From sss/alock:
python3 launch.py -u adb321 --from_param_config config.json -e testexp

## Shutdown on stall
python shutdown.py -u adb321

sh synch.sh -u adb321
bash launch_experiment.sh -no-prep
screen -c run.screenrc

# GDB Commands

### Build for GDB

cd build
cmake 


### GDB command on Node 0

gdb --args main --node_count 1 --node_id 0 --runtime 10 --op_count 100000000 --min_key 1 --max_key 1000 --region_size 22 --thread_count 3 --qp_max 3 --p_local 50 --local_budget 5 --remote_budget 5