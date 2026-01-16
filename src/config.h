#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

struct sensor {
  char *name;
  float offset;
};

struct source {
  char *name;
  char *driver;
  char *pci_device;
  int scale;

  struct sensor *sensor;
  int num_sensors;
};

struct fan {
  char *name;
  char *driver;
  char *pwm_file;
  int min_pwm;
  int max_pwm;
  bool zero_rpm;

  struct curve *curve;
};

struct graph_point {
  int temp;
  int fan_percent;
};

struct curve {
  char *name;
  struct graph_point *graph_point;
  int num_points;
};

struct config {
  int average;
  int interval;

  struct source *source;
  int num_sources;

  struct fan *fan;
  int num_fans;

  struct curve *curve;
  int num_curves;
};

int load_config(const char *path, struct config *config);
void free_config(struct config *config);

#endif
