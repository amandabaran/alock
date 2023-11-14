#!/bin/bash

# Source cluster-dependent variables
source "config2.conf"

# Use script directly on nodes or create python command to call this script on each node

mkdir mft
cd mft
wget https://www.mellanox.com/downloads/MFT/mft-4.26.0-93-x86_64-deb.tgz
tar xzvf mft-4.26.0-93-x86_64-deb.tgz 
cd mft-4.26.0-93-x86_64-deb 
sudo ./install.sh
sudo mst start