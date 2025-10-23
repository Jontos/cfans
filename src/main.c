#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config_parser.h"
#include "hwmon.h"

int debug = 0;

volatile sig_atomic_t keep_running = 1;

void signal_handler(int signum) {
  keep_running = 0;
}

int main(int argc, char *argv[]) {

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  char *config_path = "/etc/cfans/fancontrol.conf";

  int opt;
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

  Config config = {0};
  if (load_config(config_path, &config) < 0) {
    (void)fprintf(stderr, "Error loading config file: %s\n", config_path);
    exit(EXIT_FAILURE);
  }

  Graph graph = {0};
  if (load_graph(config.graph_file, &graph) < 0) {
    (void)fprintf(stderr, "Error loading graph file: %s\n", config.graph_file);
    exit(EXIT_FAILURE);
  }

  int cpu_temp;
  int gpu_temp;

  int prev_vals[config.average];
  int buf_slot = config.average;
  int first_run = 1;
  int last_val = 0;
  int written_val = -1;

  int *temperatures[config.num_sources];
  hwmonSource sources[config.num_sources];
  for (int i = 0; i < config.num_sources; i++) {
    sources[i] = hwmon_source_init(config.sources[i]);
    temperatures[i] = malloc(sources[i].num_inputs * sizeof(int));
  }

  hwmonFan fans[config.num_fans];
  for (int i = 0; i < config.num_fans; i++) {
    fans[i] = hwmon_fan_init(config.fans[i]);
    hwmon_pwm_enable(fans[i], 1);
  }


  if (debug) {
    printf("\n%s:\n", config.graph_file);
    for (int i = 0; i < graph.num_points; i++) {
      for (int j = 0; j < 2; j++) {
        printf("%i ", graph.fan_curve[i][j]);
      }
      printf("\n");
    }
    printf("Total points: %i\n\n", graph.num_points);
  }

  struct timespec interval = { .tv_nsec = 0, .tv_sec = 1 };

  while (keep_running) {
    // Reset averaging buffer index
    if (buf_slot % config.average == 0) {
      buf_slot = 0;
    }

    int highest_temp = 0;
    for (int i = 0; i < config.num_sources; i++) {
      hwmon_read_temp(sources[i], temperatures[i], config.sources[i].scale);
      for (int j = 0; j < sources[i].num_inputs; j++) {
        if (temperatures[i][j] > highest_temp) {
          highest_temp = temperatures[i][j];
        }
      }
    }

    // Populate averaging buffer with initial temp for first run
    if (first_run == 1) {
      for (int i = 0; i < config.average; i++) {
        prev_vals[i] = highest_temp;
      }
    }
    else {
      prev_vals[buf_slot] = highest_temp;
    }

    // Calculate average of previous values
    int total_vals = 0;
    for (int i = 0; i < config.average; i++) {
      total_vals += prev_vals[i];
    }
    int avg_temp = total_vals / config.average;

    buf_slot++;

    for (int i = 0; i < graph.num_points; i++) {
      if (avg_temp < graph.fan_curve[i][0]) {
        int delta_from_node = avg_temp - graph.fan_curve[i-1][0];
        int percent_diff = graph.fan_curve[i][1] - graph.fan_curve[i-1][1];
        int temp_diff = graph.fan_curve[i][0] - graph.fan_curve[i-1][0];
        int fan_percent = delta_from_node * percent_diff / temp_diff + graph.fan_curve[i-1][1];

        for (int i = 0; i < config.num_fans; i++) {
          int pwm_range = config.fans[i].max_pwm - config.fans[i].min_pwm;
          int pwm_val = fan_percent == 0 ? 0 : pwm_range / 100 * fan_percent + config.fans[i].min_pwm;

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
            hwmon_set_pwm(fans[i], last_val);
            written_val = last_val;
          }
          break;
        }
      } 
    }
    nanosleep(&interval, NULL);
  }
  // Run cleanup..
  free(graph.fan_curve);
  free_config(&config);

  printf("\nResetting automatic fan control..\n");
  for (int i = 0; i < config.num_fans; i++) {
    hwmon_pwm_enable(fans[i], 0);
  }
}
