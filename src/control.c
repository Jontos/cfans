#include <stdio.h>
#include <stdlib.h>

#include "control.h"
#include "config_parser.h"

#define ROUNDING_FLOAT 0.5F

float get_highest_temp(AppContext *app_context) {
  float highest_temp = 0;
  for (int i = 0; i < app_context->num_sources; i++) {
    float temp = hwmon_read_temp(&app_context->sources[i]);
    if (temp > highest_temp) {
      highest_temp = temp;
    }
  }
  return highest_temp;
}

int moving_average_init(AppContext *app_context, int average) {
  MovingAverage *buffer = &app_context->average_buffer;

  buffer->slot = calloc(average, sizeof(float));
  if (!buffer->slot) {
    perror("calloc");
    return -1;
  }

  float highest_temp = get_highest_temp(app_context);

  buffer->num_slots = average;
  for (int i = 0; i < buffer->num_slots; i++) {
    buffer->slot[i] = highest_temp;
  }

  return 0;
}

float moving_average_update(AppContext *app_context, float current_temp) {
  MovingAverage *buffer = &app_context->average_buffer;

  if (buffer->slot_index % buffer->num_slots == 0) {
    buffer->slot_index = 0;
  }

  buffer->slot[buffer->slot_index] = current_temp;

  float total = 0;
  for (int i = 0; i < buffer->num_slots; i++) {
    total += buffer->slot[i];
  }

  buffer->slot_index++;

  return total / (float)buffer->num_slots;
}

float linearly_interpolate(float temperature, GraphPoint *start, GraphPoint *end) {
  float fan_speed_range = end->fan_speed - start->fan_speed;
  float temp_range = end->temp - start->temp;
  float offset_from_last = temperature - start->temp;

  return start->fan_speed + (offset_from_last * fan_speed_range / temp_range);
}

float calculate_fan_percent(AppContext *app_context, float temperature) {
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

int calculate_pwm_value(float fan_percent, int min_pwm, int max_pwm) {
  float percent_decimal = fan_percent / 100.0F;
  int pwm_range = max_pwm - min_pwm;

  return min_pwm + (int)(((float)pwm_range * percent_decimal) + ROUNDING_FLOAT);
}
