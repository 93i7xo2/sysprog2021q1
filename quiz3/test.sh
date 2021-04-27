#!/bin/bash
CPUID=$(nproc --all --ignore 1)
ORIG_ASLR=$(cat /proc/sys/kernel/randomize_va_space)
ORIG_GOV=$(cat /sys/devices/system/cpu/cpu$CPUID/cpufreq/scaling_governor)
sudo bash -c "echo 0 > /proc/sys/kernel/randomize_va_space"
sudo bash -c "echo performance > /sys/devices/system/cpu/cpu$CPUID/cpufreq/scaling_governor"
sudo perf stat -e cache-misses:u taskset -c $CPUID ./xs>/dev/null
sudo perf stat -e cache-misses:u taskset -c $CPUID ./string>/dev/null
python3 driver.py
gnuplot plot.gp
sudo bash -c "echo $ORIG_ASLR > /proc/sys/kernel/randomize_va_space"
sudo bash -c "echo $ORIG_GOV > /sys/devices/system/cpu/cpu$CPUID/cpufreq/scaling_governor"