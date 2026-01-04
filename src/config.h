#ifndef CONFIG_H
#define CONFIG_H

struct sensor {
  char *name;
  int offset;
};

struct source {
  char *name;
  char *driver;
  char *pci_device;
  int scale;

  struct sensor *sensor;
  int num_sensors;
};

struct curve {
  int temp;
  int fan_percent;
};

struct fan {
  char *name;
  char *driver;
  char *pwm_file;
  int min_pwm;
  int max_pwm;

  struct curve *curve;
  int num_points;
};

struct config {
  int average;
  int interval;

  struct source *source;
  int num_sources;

  struct fan *fan;
  int num_fans;
};

int load_config(const char *path, struct config *config);
void free_config(struct config *config);

#endif
