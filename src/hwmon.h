#ifndef HWMON_H
#define HWMON_H

#include <systemd/sd-device.h>

enum scale {
  DEGREES = 1,
  MILLIDEGREES = 1000
};

struct hwmon_sensor {
  int fildes;
  float scale;
  float offset;
};

struct hwmon_fan {
  sd_device *device;

  int pwm_fildes;
  char *pwm_enable_file;
  const char *pwm_auto_control;

  int last_pwm_value;
  float target_fan_percent;
  int target_pwm_value;
};

struct app_context;
struct app_sensor;
struct config;

int hwmon_init_sources(struct config *config, struct app_context *app_context);
int hwmon_init_fans(struct config *config, struct app_context *app_context);

int hwmon_read_temp(struct app_sensor *app_sensor);
int hwmon_set_pwm(struct hwmon_fan *fan, int pwm_value);
int hwmon_restore_auto_control(struct hwmon_fan *fan);

void hwmon_destroy_sources(struct app_context *app_context);
void hwmon_destroy_fans(struct app_context *app_context);

#endif
