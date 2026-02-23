#ifndef CONTROL_H
#define CONTROL_H

#include <stdbool.h>
#include <time.h>

#include "config.h"

struct sensor_config;
struct curve_config;

struct hwmon_fan;

struct app_sensor {
  const char *name;
  const void *config;

  int (*get_temp_func)(struct app_sensor *self);
  void *sensor_data;

  float current_value;
  float target_value;
};

struct app_curve {
  struct curve_config *config;
  struct app_sensor *sensor;

  float hyst_val;
  time_t timer;
};

struct app_fan {
  struct hwmon_fan *hwmon;
  struct fan_config *config;
  struct app_curve *curve;

  float fan_percent;
  int pwm_value;
};

struct app_context {
  struct app_sensor *sensor;
  int num_sensors;
  int num_hwmon_sensors;

  struct app_fan *fan;
  int num_fans;
};

int init_custom_sensors(struct config *config, struct app_context *app_context);
int link_curve_sensors(struct app_context *app_context);

float calculate_fan_percent(struct curve_config *curve, float temperature);
int calculate_pwm_value(float fan_percent, struct fan_config *config);

void destroy_custom_sensors(struct app_context *app_context);

#endif
