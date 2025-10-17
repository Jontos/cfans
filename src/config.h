#ifndef CONFIG_H
#define CONFIG_H

#include <linux/limits.h>

#define PAGE_SIZE 4096
#define LARGE_BUFFER 1024

typedef struct {
  char **sensors;
  int num_sensors;
} hwmonDevice;

struct config {
  char pwm[PATH_MAX];
  char pwm_enable[PATH_MAX];

  hwmonDevice cpu;
  hwmonDevice gpu;

  char graph[PATH_MAX];

  int min_pwm;
  int max_pwm;
  int average;
  int interval;
};

void load_config(struct config *cfg, char *path);

int (*load_graph(int *points, char *graph))[2];

#endif
