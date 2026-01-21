#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

struct sensor_config {
  char *name;
  int offset;
};

struct source_config {
  char *name;
  char *driver;
  char *device_id;
  int scale;

  struct sensor_config *sensor;
  int num_sensors;
};

struct fan_config {
  char *name;
  char *device_id;
  char *pwm_file;
  int min_pwm;
  int max_pwm;
  bool zero_rpm;

  struct curve_config *curve;
};

struct graph_point {
  int temp;
  int fan_percent;
};

struct curve_config {
  char *name;

  struct graph_point *graph_point;
  int num_points;

  char *sensor;

  int hysteresis;
  int response_time;
};

struct custom_sensor_config {
  char *name;
  char *type;

  struct sensor_config *sensor;
  int num_sensors;
};

struct config {
  int average;
  long interval;

  struct source_config *source;
  int num_sources;

  struct fan_config *fan;
  int num_fans;

  struct curve_config *curve;
  int num_curves;

  struct custom_sensor_config *custom_sensor;
  int num_custom_sensors;
};

int load_config(const char *path, struct config *config);
void free_config(struct config *config);

#endif
