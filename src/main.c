#include "config.h"

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int debug = 0;

volatile sig_atomic_t keep_running = 1;

struct required_key {
  const char *name;
  bool *is_set;
};

void enable_pwm(struct config *cfg, char *og_value) {
  FILE *file = fopen(cfg->pwm_enable, "r+");
  if (!file) {
    fprintf(stderr, "Failed to open %s: %s\n", cfg->pwm_enable, strerror(errno));
    exit(EXIT_FAILURE);
  }

  if (fgets(og_value, sizeof(og_value), file) == NULL) {
    fprintf(stderr, "Failed to read %s: %s\n", cfg->pwm_enable, strerror(errno));
  }

  if (fputc('1', file) == EOF) {
    fprintf(stderr, "Failed to write to %s: %s\n", cfg->pwm_enable, strerror(errno));
    exit(EXIT_FAILURE);
  }
  fclose(file);
}

void set_pwm(int val, char *pwm) {
  char val_buf[32];
  snprintf(val_buf, sizeof(val_buf), "%i", val);

  FILE *file = fopen(pwm, "w");
  if (!file) {
    fprintf(stderr, "Failed to open %s: %s\n", pwm, strerror(errno));
    exit(EXIT_FAILURE);
  }

  if (fputs(val_buf, file) == EOF) {
    fprintf(stderr, "Failed to write to %s: %s\n", pwm, strerror(errno));
    exit(EXIT_FAILURE);
  }
  fclose(file);
}

void read_temp(hwmonDevice device, int *temp) {
  FILE *file;
  char buffer[32];
  int highest_temp = 0;

  for (int i = 0; i < device.num_sensors; i++) {
    file = fopen(device.sensors[i], "r");
    if (!file) {
      fprintf(stderr, "fopen: failed to open %s\n", device.sensors[i]);
      exit(EXIT_FAILURE);
    }

    if (fgets(buffer, sizeof(buffer), file) == NULL) {
      fprintf(stderr, "fgets: failed to read %s\n", device.sensors[i]);
      exit(EXIT_FAILURE);
    }
    fclose(file);

    int temp_buffer = atoi(buffer) / 1000;
    highest_temp = (temp_buffer > highest_temp) ? temp_buffer : highest_temp;
  }
  *temp = highest_temp;
}

void signal_handler(int signum) {
  keep_running = 0;
}

int main(int argc, char *argv[]) {
  int opt;
  char *config_path = "/etc/cfans/fancontrol.conf";
  struct config cfg;

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  while ((opt = getopt(argc, argv, "c:d")) != -1) {
    switch (opt) {
      case 'c':
        config_path = optarg;
        break;
      case 'd':
        debug = 1;
        break;
      case '?':
        fprintf(stderr, "Usage: %s [-c CONFIG_FILE] [-d]\n", argv[0]);
        exit(EXIT_FAILURE);
    } 
  }

  load_config(&cfg, config_path);

  int cpu_temp;
  int gpu_temp;

  int prev_vals[cfg.average];
  int buf_slot = cfg.average;
  int first_run = 1;
  int last_val = 0;
  int written_val = -1;

  int points;
  int (*graph_curve)[2];
  graph_curve = load_graph(&points, cfg.graph);

  char og_pwm_enable_value[4];
  enable_pwm(&cfg, og_pwm_enable_value);

  if (debug) {
    printf("\n%s:\n", cfg.graph);
    for (int i = 0; i < points; i++) {
      for (int j = 0; j < 2; j++) {
        printf("%i ", graph_curve[i][j]);
      }
      printf("\n");
    }
    printf("Total points: %i\n\n", points);
  }

  while (keep_running) {
    // Reset averaging buffer index
    if (buf_slot % cfg.average == 0) {
      buf_slot = 0;
    }

    read_temp(cfg.cpu, &cpu_temp);
    read_temp(cfg.gpu, &gpu_temp);
    int highest_temp = (gpu_temp > cpu_temp) ? gpu_temp : cpu_temp;

    // Populate averaging buffer with initial temp for first run
    if (first_run == 1) {
      for (int i = 0; i < cfg.average; i++) {
        prev_vals[i] = highest_temp;
      }
    }
    else {
      prev_vals[buf_slot] = highest_temp;
    }

    // Calculate average of previous values
    int total_vals = 0;
    for (int i = 0; i < cfg.average; i++) {
      total_vals += prev_vals[i];
    }
    int avg_temp = total_vals / cfg.average;

    buf_slot++;

    for (int i = 0; i < points; i++) {
      if (avg_temp < graph_curve[i][0]) {
        int delta_from_node = avg_temp - graph_curve[i-1][0];
        int percent_diff = graph_curve[i][1] - graph_curve[i-1][1];
        int temp_diff = graph_curve[i][0] - graph_curve[i-1][0];
        int fan_percent = delta_from_node * percent_diff / temp_diff + graph_curve[i-1][1];

        int pwm_range = cfg.max_pwm - cfg.min_pwm;
        int pwm_val = fan_percent == 0 ? 0 : pwm_range / 100 * fan_percent + cfg.min_pwm;

        if (first_run == 1) {
          last_val = pwm_val;
          first_run = 0;
        }
        else if (pwm_val > last_val) {
          last_val++;
        }
        else if (pwm_val < last_val) {
          last_val--;
        }

        if (debug) {
          printf("\ravg: %i fan: %i pwm: %i last: %i       ",
                  avg_temp, fan_percent, pwm_val, last_val);
          fflush(stdout);
        }

        if (last_val != written_val) {
          set_pwm(last_val, cfg.pwm);
          written_val = last_val;
        }
        break;
      } 
    }
    struct timespec interval;
    interval.tv_nsec = cfg.interval;
    nanosleep(&interval, NULL);
  }
  // Run cleanup..
  free(graph_curve);
  free(cfg.cpu.sensors);
  free(cfg.gpu.sensors);

  printf("\nResetting automatic fan control..\n");

  FILE *file = fopen(cfg.pwm_enable, "w");
  if (!file) {
    fprintf(stderr, "Failed to open %s: %s\n", cfg.pwm_enable, strerror(errno));
    exit(EXIT_FAILURE);
  }

  if (fputs(og_pwm_enable_value, file) == EOF) {
    fprintf(stderr, "Failed to write to %s: %s\n", cfg.pwm_enable, strerror(errno));
    exit(EXIT_FAILURE);
  }
  fclose(file);

}
