# ALock README


## Setup

First, in a directory containing this alock repo, downlaod the rome repo using the 'sss-tutorials' branch. https://github.com/amandabaran/rome.git

In order to run the experiments, you first need to create a CloudLab experiment with a cluster of 20 r320 nodes.
Copy the node information in csv format into the nodefile, found in alock/benchmark/nodefiles/r320.csv.

From alock/scripts, you can run the cloudlab_setup.sh script. This will install all necessary packages and dependencies needed on the nodes that are in the r320.csv nodefile.


## Experiments

In order to recreate Figure 1, the study showing the effects of loopback traffic using a spinlock, you can run the spin_exp.sh script. 

To recreate Figure 4, the study showing the effects of the remote and local budget, you can run the budget_exp.sh script.

To recreate Figures 5 and 6, the plots showing the throughput and latency of the alock and its competitiors, you can run the scalability_exp.sh script. 

## Plots

In order to plot the data, run the plot.sh script with the name of the directory to be plotted.

For example, plotting the scalability experiment would be the command: 
"zsh plot.sh scalability_exp"


## Commands

# Sync
sh sync.sh -u adb321

# Install Dependencies 
From remus/scripts:
<!-- -i installs dependencies for first time connecting to nodes -->
sh sync.sh -u adb321 -i 