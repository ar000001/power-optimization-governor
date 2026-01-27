#!/bin/bash

Graph=$1
N_Frames=$2
PartitionPoint1=$3
PartitionPoint2=$4
Order=$5

adb -d root
adb -d shell "export LD_LIBRARY_PATH=/data/local/Working_dir && cd /data/local/Working_dir && ./${Graph} --threads=4  --threads2=2 --target=CL --n=${N_Frames} --partition_point=${PartitionPoint1} --partition_point2=${PartitionPoint2} --order=${Order} > last_run_output.txt"
adb -d pull /data/local/Working_dir/last_run_output.txt last_run_output.txt