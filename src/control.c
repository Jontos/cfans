#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "control.h"
#include "config.h"

#define ROUNDING_FLOAT 0.5F

struct custom_sensor_data {
  struct app_sensor *sensor;
  int num_sensors;
};

static int get_max_temp(struct app_sensor *self)
{
  struct custom_sensor_data *data = self->sensor_data;

  data->sensor[0].get_temp_func(&data->sensor[0]);
  self->current_value = data->sensor[0].current_value;

  for (int i = 1; i < data->num_sensors; i++) {
    data->sensor[i].get_temp_func(&data->sensor[i]);
    self->current_value = data->sensor[i].current_value > self->current_value
      ? data->sensor[i].current_value
      : self->current_value;
  }

  return 0;
}

int link_curve_sensors(struct app_context *app_context)
{
  for (int i = 0; i < app_context->num_fans; i++) {
    for (int j = 0; j < app_context->num_sensors; j++) {
      if (strcmp(app_context->fan[i].curve->config->sensor, app_context->sensor[j].name) != 0) {
        if (j == app_context->num_sensors - 1) {
          (void)fprintf(stderr, "No sensor \"%s\" found for curve \"%s\"\n",
                        app_context->fan[i].curve->config->sensor,
                        app_context->fan[i].curve->config->name);
          return -1;
        }
        continue;
      }
      app_context->fan[i].curve->sensor = &app_context->sensor[j];
    }
  }

  return 0;
}

static int match_sensor_type(const char *type_string, struct app_sensor *app_sensor)
{
  struct {
    const char *name;
    int (*get_temp_func)(struct app_sensor *self);
  }
  func[] = { {"max", get_max_temp} };

  for (int i = 0; i < (int)(sizeof(func) / sizeof(func[i])); i++) {
    if (strcmp(type_string, func[i].name) == 0) {
      app_sensor->get_temp_func = func[i].get_temp_func;
      return 0;
    }
  }

  (void)fprintf(stderr, "No custom sensor type \"%s\"\n", type_string);
  return -1;
}

static int link_sensor_array(struct app_context *app_context,
                      struct custom_sensor_config *config,
                      int app_sensor_num)
{
  struct custom_sensor_data *data = malloc(sizeof(struct custom_sensor_data));
  if (!data) {
    perror("Failed to allocate custom_sensor_data");
    return -1;
  }
  data->sensor = calloc(config->num_sensors, sizeof(struct app_sensor));
  if (!data->sensor) {
    perror("Failed to allocate sensor_config array");
    return -1;
  }
  data->num_sensors = config->num_sensors;

  int count = 0;
  for (int i = 0; i < config->num_sensors; i++) {
    for (int j = 0; j < app_context->num_sensors; j++) {
      if (strcmp(config->sensor[i].name, app_context->sensor[j].name) != 0) {
        if (j == app_context->num_sensors - 1) {
          (void)fprintf(stderr, "Couldn't find sensor \"%s\" for \"%s\"\n",
                        config->sensor[i].name, config->name);
          return -1;
        }
        continue;
      }
      data->sensor[count++] = app_context->sensor[j];
      break;
    }
  }

  app_context->sensor[app_sensor_num].sensor_data = data;

  return 0;
}

int init_custom_sensors(struct config *config, struct app_context *app_context)
{
  app_context->sensor = reallocarray(app_context->sensor,
                                     app_context->num_sensors + config->num_custom_sensors,
                                     sizeof(struct app_sensor));
  if (!app_context->sensor) {
    perror("Failed to reallocate app_sensor array");
    return -1;
  }

  for (int i = 0; i < config->num_custom_sensors; i++) {
    app_context->sensor[app_context->num_sensors].name = config->custom_sensor[i].name;
    app_context->sensor[app_context->num_sensors].config = &config->custom_sensor[i];
    if (match_sensor_type(config->custom_sensor[i].type,
                          &app_context->sensor[app_context->num_sensors]) < 0 ||
    link_sensor_array(app_context, &config->custom_sensor[i],
                      app_context->num_sensors) < 0)
    {
      return -1;
    }
    app_context->num_sensors++;
  }

  return 0;
}

void destroy_custom_sensors(struct app_context *app_context)
{
  for (int i = app_context->num_hwmon_sensors; i < app_context->num_sensors; i++) {
    free(((struct custom_sensor_data*)app_context->sensor[i].sensor_data)->sensor);
    free(app_context->sensor[i].sensor_data);
  }
  free(app_context->sensor);
}

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

int calculate_pwm_value(float fan_percent, struct fan_config *config)
{
  if (config->zero_rpm && fan_percent == 0) {
    return 0;
  }

  float percent_decimal = fan_percent / 100.0F;
  int pwm_range = config->max_pwm - config->min_pwm;

  return config->min_pwm + (int)(((float)pwm_range * percent_decimal) + ROUNDING_FLOAT);
}
