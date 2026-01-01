#ifndef HWMON_H
#define HWMON_H

#include <systemd/sd-device.h>

#include "config_parser.h"

typedef struct {
    char *name;
    char *filename;
} tempInput;

typedef struct {
    char *name;
    sd_device_enumerator *enumerator;
    sd_device *device;
    tempInput *temp_inputs;
    int num_inputs;
    int input_capacity;
    char *hottest_sensor;
    float scale;
} hwmonSource;

typedef struct {
    sd_device_enumerator *enumerator;
    sd_device *device;
    char *pwm_file;
    char *pwm_enable_file;

    int last_pwm_value;
} hwmonFan;

int hwmon_source_init(Source *config, hwmonSource *source);
int hwmon_fan_init(Fan *config, hwmonFan *fan);

float hwmon_read_temp(hwmonSource *source);
int hwmon_set_pwm(hwmonFan *fan, int pwm_value);
int hwmon_restore_auto_control(hwmonFan *fan);

void hwmon_source_destroy(hwmonSource *sources, int num_sources);
void hwmon_fan_destroy(hwmonFan *fans, int num_fans);

#endif
