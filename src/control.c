#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include "control.h"
#include "config.h"

#define ROUNDING_FLOAT 0.5F
#define TEMP_INPUT_SIZE 32

struct custom_sensor_data {
  struct app_sensor *sensor;
  float *offset;
  int num_sensors;
};

struct file_sensor_data {
  char *path;
  int fildes;
};

static int get_max_temp(struct app_sensor *self)
{
  struct custom_sensor_data *data = self->sensor_data;

  data->sensor[0].get_temp_func(&data->sensor[0]);
  self->current_value = data->sensor[0].current_value + data->offset[0];

  for (int i = 1; i < data->num_sensors; i++) {
    data->sensor[i].get_temp_func(&data->sensor[i]);
    self->current_value = data->sensor[i].current_value + data->offset[i] > self->current_value
      ? data->sensor[i].current_value + data->offset[i]
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
      break;
    }
  }

  return 0;
}

int file_read_temp(struct app_sensor *self)
{
  struct file_sensor_data *data = self->sensor_data;

  char temp_input_string[TEMP_INPUT_SIZE];
  ssize_t nread = pread(data->fildes, temp_input_string, TEMP_INPUT_SIZE - 1, 0);
  if (nread < 0) {
    (void)fprintf(stderr, "Error: couldn't read %s: %s\n", data->path, strerror(errno));
    return -1;
  }
  
  temp_input_string[nread] = '\0';

  self->current_value = strtof(temp_input_string, NULL);

  return 0;
}

static int link_sensor_array(struct app_context *app_context,
                      struct custom_sensor_config *config)
{
  struct custom_sensor_data *data = malloc(sizeof(*data));
  if (!data) {
    perror("Failed to allocate custom_sensor_data");
    return -1;
  }
  data->sensor = calloc(config->type_opts.max.num_sensors, sizeof(*data->sensor));
  if (!data->sensor) {
    perror("Failed to allocate sensor_config array");
    return -1;
  }
  data->offset = calloc(config->type_opts.max.num_sensors, sizeof(*data->offset));
  if (!data->offset) {
    perror("Failed to allocate offset array");
    return -1;
  }
  data->num_sensors = config->type_opts.max.num_sensors;

  int count = 0;
  for (int i = 0; i < config->type_opts.max.num_sensors; i++) {
    for (int j = 0; j < app_context->num_sensors; j++) {
      if (strcmp(config->type_opts.max.sensor[i].name, app_context->sensor[j].name) != 0) {
        if (j == app_context->num_sensors - 1) {
          (void)fprintf(stderr, "Couldn't find sensor \"%s\" for \"%s\"\n",
                        config->type_opts.max.sensor[i].name, config->name);
          return -1;
        }
        continue;
      }
      data->sensor[count] = app_context->sensor[j];
      data->offset[count] = config->type_opts.max.sensor[i].offset;
      count++;

      break;
    }
  }

  app_context->sensor[app_context->num_sensors].sensor_data = data;

  return 0;
}

int link_file_path(struct app_context *app_context,
                   struct custom_sensor_config *config)
{
  struct file_sensor_data *data = malloc(sizeof(*data));

  if (config->type_opts.file.path[0] == '~') {
    char *dest_path = &config->type_opts.file.path[1];
    const char *home_path = getenv("HOME");
    if (!home_path) {
      (void)fprintf(stderr, "Couldn't expand tilde, is $HOME set?\n");
      return -1;
    }

    asprintf(&data->path, "%s%s", home_path, dest_path);
    if (!data->path) {
      perror("asprintf");
      return -1;
    }

  }
  else {
    data->path = strdup(config->type_opts.file.path);
  }

  data->fildes = open(data->path, O_RDONLY);
  if (data->fildes < 0) {
    (void)fprintf(stderr, "Failed to open %s: %s\n", data->path, strerror(errno));
    return -1;
  }

  app_context->sensor[app_context->num_sensors].sensor_data = data;

  return 0;
}

static int match_sensor_type(struct custom_sensor_config *config, struct app_context *app_context)
{
  struct {
    const char *name;
    int (*get_temp_func)(struct app_sensor *self);
    int (*setup_func)(struct app_context *app_context, struct custom_sensor_config *config);
  }
  type[] = {
    {"max", get_max_temp, link_sensor_array},
    {"file", file_read_temp, link_file_path},
  };

  for (int i = 0; i < (int)(sizeof(type) / sizeof(type[i])); i++) {
    if (strcmp(config->type, type[i].name) == 0) {
      app_context->sensor[app_context->num_sensors].get_temp_func = type[i].get_temp_func;
      return type[i].setup_func(app_context, config);
    }
  }

  (void)fprintf(stderr, "No custom sensor type \"%s\"\n", config->type);
  return -1;
}

int init_custom_sensors(struct config *config, struct app_context *app_context)
{
  void *new_array = reallocarray(app_context->sensor,
                                     app_context->num_sensors + config->num_custom_sensors,
                                     sizeof(struct app_sensor));
  if (!new_array) {
    perror("Failed to reallocate app_sensor array");
    return -1;
  }
  app_context->sensor = new_array;

  for (int i = 0; i < config->num_custom_sensors; i++) {
    app_context->sensor[app_context->num_sensors].name = config->custom_sensor[i].name;
    app_context->sensor[app_context->num_sensors].config = &config->custom_sensor[i];
    if (match_sensor_type(&config->custom_sensor[i], app_context) < 0) return -1;
    app_context->num_sensors++;
  }

  return 0;
}

void destroy_custom_sensors(struct app_context *app_context)
{
  for (int i = app_context->num_hwmon_sensors; i < app_context->num_sensors; i++) {
    if (strcmp(((struct custom_sensor_config*)app_context->sensor[i].config)->type, "max") == 0) {
      free(((struct custom_sensor_data*)app_context->sensor[i].sensor_data)->sensor);
      free(((struct custom_sensor_data*)app_context->sensor[i].sensor_data)->offset);
    }
    else if (strcmp(((struct custom_sensor_config*)app_context->sensor[i].config)->type, "file") == 0) {
      free(((struct file_sensor_data*)app_context->sensor[i].sensor_data)->path);
    }
    free(app_context->sensor[i].sensor_data);
  }
  free(app_context->sensor);
}

float linearly_interpolate(float temperature, struct graph_point *start, struct graph_point *end)
{
  float fan_speed_range = end->fan_percent - start->fan_percent;
  float temp_range = end->temp - start->temp;
  float offset_from_last = temperature - start->temp;

  return start->fan_percent + (offset_from_last * fan_speed_range / temp_range);
}

float calculate_fan_percent(struct curve_config *curve, float temperature)
{
  int high = curve->num_points - 1;
  int low = 0;

  if (temperature <= curve->graph_point[0].temp) {
    return curve->graph_point[0].fan_percent;
  }
  if (temperature >= curve->graph_point[curve->num_points - 1].temp) {
    return curve->graph_point[curve->num_points - 1].fan_percent;
  }

  while (low <= high) {
    int mid = low + ((high - low) / 2);
    if (temperature == curve->graph_point[mid].temp) {
      return curve->graph_point[mid].fan_percent;
    }
    if (temperature < curve->graph_point[mid].temp) {
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
  float pwm_range = config->max_pwm - config->min_pwm;

  return (int)(config->min_pwm + ((pwm_range * percent_decimal) + ROUNDING_FLOAT));
}
