#!/bin/bash

source "../exp.conf"

mode=top # top or rand

path="results/${mode}/t${thread_count}"

python3 plot.py --save_dir "../${path}" --resfile '_result.csv' --exp 'tput'