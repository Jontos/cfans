#ifndef HWMON_H
#define HWMON_H

#include <stdbool.h>

#include "config_parser.h"

typedef struct {
    char **temp_inputs;
    int scale;
    int num_inputs;
    int input_capacity;
} hwmonSource;

typedef struct {
    char *pwm_file_path;
    char *pwm_enable_file_path;

    int last_pwm_value;
} hwmonFan;

int hwmon_source_init(Source config, hwmonSource *source);
int hwmon_fan_init(Fan config, hwmonFan *fan);

int hwmon_pwm_enable(hwmonFan fan, int mode);
int hwmon_read_temp(hwmonSource source, int scale);
int hwmon_set_pwm(hwmonFan fan, int value);

void hwmon_source_destroy(hwmonSource *sources, int num_sources);
void hwmon_fan_destroy(hwmonFan *fans, int num_fans);

#endif
