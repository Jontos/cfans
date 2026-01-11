#include <stdio.h>
#include <stdlib.h>

#include "control.h"

#define ROUNDING_FLOAT 0.5F

float get_highest_temp(AppContext *app_context)
{
  float highest_temp = 0;
  for (int i = 0; i < app_context->num_sources; i++) {
    for (int j = 0; j < app_context->source[i].num_inputs; j++) {
      hwmon_read_temp(&app_context->source[i].temp_input[j]);
      if (app_context->source[i].temp_input[j].current_temp > highest_temp) {
        highest_temp = app_context->source[i].temp_input[j].current_temp;
        app_context->hottest_device = app_context->source[i].name;
        app_context->hottest_sensor = app_context->source[i].temp_input[j].name;
      }
    }
  }

  return highest_temp;
}

int moving_average_init(AppContext *app_context, int average) {
  MovingAverage *buffer = &app_context->average_buffer;

  buffer->slot = calloc(average, sizeof(float));
  if (buffer->slot == NULL) {
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

float linearly_interpolate(float temperature, struct curve *start, struct curve *end) {
  float fan_speed_range = (float)end->fan_percent - (float)start->fan_percent;
  float temp_range = (float)end->temp - (float)start->temp;
  float offset_from_last = temperature - (float)start->temp;

  return (float)start->fan_percent + (offset_from_last * fan_speed_range / temp_range);
}

float calculate_fan_percent(struct curve curve[], int num_points, float temperature)
{
  int high = num_points - 1;
  int low = 0;

  if (temperature <= (float)curve[0].temp) {
    return (float)curve[0].fan_percent;
  }
  if (temperature >= (float)curve[num_points-1].temp) {
    return (float)curve[num_points-1].fan_percent;
  }

  while (low <= high) {
    int mid = low + ((high - low) / 2);
    if (temperature == (float)curve[mid].temp) {
      return (float)curve[mid].fan_percent;
    }
    if (temperature < (float)curve[mid].temp) {
      high = mid - 1;
    }
    else {
      low = mid + 1;
    }
  }

  return
  linearly_interpolate(temperature, &curve[high], &curve[low]);
}

int calculate_pwm_value(float fan_percent, int min_pwm, int max_pwm, bool zero_rpm)
{
  if (zero_rpm && fan_percent == 0) {
    return 0;
  }

  float percent_decimal = fan_percent / 100.0F;
  int pwm_range = max_pwm - min_pwm;

  return min_pwm + (int)(((float)pwm_range * percent_decimal) + ROUNDING_FLOAT);
}
