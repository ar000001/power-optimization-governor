#!/bin/bash

CpuType=$1
Freq=$2

LittleFrequencyTable=(500000 667000 1000000 1200000 1398000 1512000 1608000 1704000 1800000)
BigFrequencyTable=(500000 667000 1000000 1200000 1398000 1512000 1608000 1704000 1800000 1908000 2016000 2100000 2208000)

if [ "$CpuType" == "little" ]; then
    if ! [[ " ${LittleFrequencyTable[@]} " =~ " ${Freq} " ]]; then
        echo "Error: Freq must be a valid frequency for the little CPU"
        exit 1
    fi
elif [ "$CpuType" == "big" ]; then
    if ! [[ " ${BigFrequencyTable[@]} " =~ " ${Freq} " ]]; then
        echo "Error: Freq must be a valid frequency for the big CPU"
        exit 1
    fi
else
    echo "Error: CpuType must be either 'little' or 'big'"
    exit 1
fi

adb -d root

if [ "$CpuType" == "little" ]; then
    adb -d shell "echo performance > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor"
    adb -d shell "echo ${Freq} > /sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq"

    if [ "$(adb -d shell "cat /sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq")" != "${Freq}" ]; then
        echo "Error: Frequency was not set correctly"
        exit 1
    fi
else
    adb -d shell "echo performance > /sys/devices/system/cpu/cpufreq/policy2/scaling_governor"
    adb -d shell "echo ${Freq} > /sys/devices/system/cpu/cpufreq/policy2/scaling_max_freq"

    if [ "$(adb -d shell "cat /sys/devices/system/cpu/cpufreq/policy2/scaling_max_freq")" != "${Freq}" ]; then
        echo "Error: Frequency was not set correctly"
        exit 1
    fi
fi