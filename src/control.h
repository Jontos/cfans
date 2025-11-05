#ifndef CONTROL_H
#define CONTROL_H

#include <stdbool.h>

#include "config_parser.h"
#include "hwmon.h"

typedef struct {
  float *slot;
  int slot_index;
  int num_slots;
} MovingAverage;

typedef struct {
  Graph graph;

  hwmonSource *sources;
  int num_sources;
  int initialised_sources;

  hwmonFan *fans;
  int num_fans;
  int initialised_fans;

  MovingAverage average_buffer;

  bool debug;

} AppContext;

float get_highest_temp(AppContext *app_context);

int moving_average_init(AppContext *app_context, int average);
float moving_average_update(AppContext *app_context, float current_temp);

float calculate_fan_percent(AppContext *app_context, float temperature);
int calculate_pwm_value(float fan_percent, int min_pwm, int max_pwm);

#endif
