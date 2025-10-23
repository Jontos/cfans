#ifndef HWMON_H
#define HWMON_H

#include <stdbool.h>

#include "config_parser.h"

typedef struct {
    char **temp_inputs;
    int num_inputs;
    int input_capacity;
} hwmonSource;

typedef struct {
    char *pwm_file;
    char *pwm_enable_file;
} hwmonFan;

hwmonSource hwmon_source_init(Source source);
hwmonFan hwmon_fan_init(Fan fan);

int hwmon_pwm_enable(hwmonFan fan, int mode);
int hwmon_read_temp(hwmonSource source, int *temperatures, int scale);
int hwmon_set_pwm(hwmonFan fan, int value);

#endif
