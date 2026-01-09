
#!/bin/bash

Enable=$1
Mode=$2
Level=$3

adb -d root

adb -d shell "echo ${Enable} > /sys/class/fan/enable"
adb -d shell "echo ${Mode} > /sys/class/fan/mode"
adb -d shell "echo ${Level} > /sys/class/fan/level"