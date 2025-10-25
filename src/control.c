#include <stdio.h>
#include <stdlib.h>

#include "control.h"
#include "config_parser.h"

int get_highest_temp(AppContext *app_context) {
  int highest_temp = 0;
  for (int i = 0; i < app_context->num_sources; i++) {
    int temp = hwmon_read_temp(app_context->sources[i], app_context->sources[i].scale);
    if (temp > highest_temp) {
      highest_temp = temp;
    }
  }
  return highest_temp;
}

int moving_average_init(AppContext *app_context, int average) {
  MovingAverage *buffer = &app_context->average_buffer;

  buffer->slot = calloc(average, sizeof(int));
  if (!buffer->slot) {
    perror("calloc");
    return -1;
  }

  int highest_temp = get_highest_temp(app_context);

  buffer->num_slots = average;
  for (int i = 0; i < buffer->num_slots; i++) {
    buffer->slot[i] = highest_temp;
  }

  return 0;
}

int moving_average_update(AppContext *app_context, int current_temp) {
  MovingAverage *buffer = &app_context->average_buffer;

  if (buffer->slot_index % buffer->num_slots == 0) {
    buffer->slot_index = 0;
  }

  buffer->slot[buffer->slot_index] = current_temp;

  int total = 0;
  for (int i = 0; i < buffer->num_slots; i++) {
    total += buffer->slot[i];
  }

  buffer->slot_index++;

  return total / buffer->num_slots;
}

int linearly_interpolate(int temperature, GraphPoint *start, GraphPoint *end) {
  int fan_speed_range = end->fan_speed - start->fan_speed;
  int temp_range = end->temp - start->temp;
  int offset_from_last = temperature - start->temp;

  return start->fan_speed + (offset_from_last * fan_speed_range / temp_range);
}

int calculate_fan_percent(AppContext *app_context, int temperature) {
  Graph *graph = &app_context->graph;

  for (int i = 0; i < graph->num_points; i++) {
    if (temperature < graph->points[i].temp) {
      if (i == 0) {
        return graph->points[0].fan_speed;
      }
      return linearly_interpolate(temperature, &graph->points[i-1], &graph->points[i]);
    }
  }
  return graph->points[graph->num_points-1].fan_speed;
}

int calculate_pwm_value(int fan_percent, int min_pwm, int max_pwm) {
  float percent_decimal = (float)fan_percent / 100.0F;
  float pwm_range = (float)max_pwm - (float)min_pwm;

  return min_pwm + (int)(pwm_range * percent_decimal);
}
