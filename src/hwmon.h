#ifndef HWMON_H
#define HWMON_H

#include <systemd/sd-device.h>

#include "config.h"

struct temp_input {
  char *name;
  char *filename;
};

struct hwmon_source {
  char *name;
  sd_device_enumerator *enumerator;
  sd_device *device;
  struct temp_input *temp_input;
  int num_inputs;
  char *hottest_sensor;
  float scale;
};

struct hwmon_fan {
  sd_device_enumerator *enumerator;
  sd_device *device;
  char *pwm_file;
  char *pwm_enable_file;

  int last_pwm_value;
};

int hwmon_source_init(struct source *config, struct hwmon_source *source);
int hwmon_fan_init(struct fan *config, struct hwmon_fan *fan);

float hwmon_read_temp(struct hwmon_source *source);
int hwmon_set_pwm(struct hwmon_fan *fan, int pwm_value);
int hwmon_restore_auto_control(struct hwmon_fan *fan);

void hwmon_source_destroy(struct hwmon_source *sources, int num_sources);
void hwmon_fan_destroy(struct hwmon_fan *fans, int num_fans);

#endif
