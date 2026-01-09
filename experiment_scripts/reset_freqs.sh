#!/bin/bash

# policy0 = LittleCPU
# policy2 = bigCPU  

adb -d root

# Reset scaling_policy to interactive

adb -d shell "echo interactive > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor"
adb -d shell "echo interactive > /sys/devices/system/cpu/cpufreq/policy2/scaling_governor"

# Reset scaling_max_freq to corresponding max frequencies

adb -d shell "echo 1800000 > /sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq"
adb -d shell "echo 2208000 > /sys/devices/system/cpu/cpufreq/policy2/scaling_max_freq"
