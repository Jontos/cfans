#!/bin/bash

ENABLE=/sys/class/hwmon/hwmon4/pwm3_enable
PWM=/sys/class/hwmon/hwmon4/pwm3
CPU_TEMP=/sys/class/hwmon/hwmon3/temp1_input
GPU_TEMP=/sys/class/hwmon/hwmon5/temp1_input
MIN_TEMP=50
MAX_TEMP=90
MIN_PWM=40
MAX_PWM=255

echo 1 > "$ENABLE"

while true; do
  cpu_temp=$(cat $CPU_TEMP)
  gpu_temp=$(cat $GPU_TEMP)
  if (( gpu_temp > cpu_temp )); then
    temp=$gpu_temp
  else
    temp=$cpu_temp
  fi

  if (( temp / 1000 < MIN_TEMP )); then
    echo 0 > $PWM
  else
    echo $(((temp / 1000 - MIN_TEMP) * (MAX_PWM - MIN_PWM) / (MAX_TEMP - MIN_TEMP) + MIN_PWM )) > $PWM
  fi
  sleep 3
done
