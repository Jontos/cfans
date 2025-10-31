#ifndef CONTROL_H
#define CONTROL_H

#include <stdbool.h>

#include "config_parser.h"
#include "hwmon.h"

typedef struct {
  int *slot;
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

int get_highest_temp(AppContext *app_context);

int moving_average_init(AppContext *app_context, int average);
int moving_average_update(AppContext *app_context, int current_temp);

int calculate_fan_percent(AppContext *app_context, int temperature);
int calculate_pwm_value(int fan_percent, int min_pwm, int max_pwm);

#endif
