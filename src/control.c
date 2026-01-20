#include "control.h"
#include "config.h"

#define ROUNDING_FLOAT 0.5F

float linearly_interpolate(float temperature, struct graph_point *start, struct graph_point *end)
{
  float fan_speed_range = (float)end->fan_percent - (float)start->fan_percent;
  float temp_range = (float)end->temp - (float)start->temp;
  float offset_from_last = temperature - (float)start->temp;

  return (float)start->fan_percent + (offset_from_last * fan_speed_range / temp_range);
}

float calculate_fan_percent(struct curve_config *curve, float temperature)
{
  int high = curve->num_points - 1;
  int low = 0;

  if (temperature <= (float)curve->graph_point[0].temp) {
    return (float)curve->graph_point[0].fan_percent;
  }
  if (temperature >= (float)curve->graph_point[curve->num_points - 1].temp) {
    return (float)curve->graph_point[curve->num_points - 1].fan_percent;
  }

  while (low <= high) {
    int mid = low + ((high - low) / 2);
    if (temperature == (float)curve->graph_point[mid].temp) {
      return (float)curve->graph_point[mid].fan_percent;
    }
    if (temperature < (float)curve->graph_point[mid].temp) {
      high = mid - 1;
    }
    else {
      low = mid + 1;
    }
  }

  return
  linearly_interpolate(temperature, &curve->graph_point[high], &curve->graph_point[low]);
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
