#!/bin/bash

Graph=$1
N_Frames=$2
PartitionPoint1=$3
PartitionPoint2=$4
Order=$5

adb -d root
adb -d shell "cd /data/local/Working_dir && ./${Graph} --threads=4  --threads2=2  --target=NEON --n=${N_Frames} --partition_point=${PartitionPoint1} --partition_point2=${PartitionPoint2} --order=${Order}"