#ifndef CONTROL_H
#define CONTROL_H

#include "config.h"
#include <stdbool.h>

struct sensor_config;
struct curve_config;

struct hwmon_fan;

typedef struct {
  float *slot;
  int slot_index;
  int num_slots;
} MovingAverage;

struct app_sensor {
  const struct sensor_config *config;

  float (*get_temp_func)(void *data);
  void *data;

  float current_value;
};

struct app_curve {
  struct curve_config *config;
  struct app_sensor *sensor;
};

struct app_fan {
  struct hwmon_fan *hwmon;
  struct fan_config *config;
  struct app_curve *curve;
};

struct app_context {
  struct app_sensor *sensor;
  int num_sensors;

  struct app_fan *fan;
  int num_fans;

  MovingAverage average_buffer;

  bool debug;

  char *hottest_device;
  char *hottest_sensor;
};

float get_highest_temp(struct app_context *app_context);

int moving_average_init(struct app_context *app_context, int average);
float moving_average_update(struct app_context *app_context, float current_temp);

float calculate_fan_percent(struct curve_config *curve, float temperature);
int calculate_pwm_value(float fan_percent, int min_pwm, int max_pwm, bool zero_rpm);

#endif
