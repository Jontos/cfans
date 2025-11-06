#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "config_parser.h"
#include "control.h"
#include "hwmon.h"

#define MILLISECOND (int)1e6

volatile sig_atomic_t keep_running = 1;

void signal_handler(int signum) {
  if (signum == SIGINT || signum == SIGTERM) {
    keep_running = 0;
  }
}

void destroy_hardware(AppContext *app_context) {
  if (app_context == NULL) {
    return;
  }

  if (app_context->sources) {
    hwmon_source_destroy(app_context->sources, app_context->initialised_sources);
  }
  if (app_context->fans) {
    hwmon_fan_destroy(app_context->fans, app_context->initialised_fans);
  }

  free(app_context->sources);
  free(app_context->fans);
  free(app_context->graph.points);
  free(app_context->average_buffer.slot);
}

int init_hardware(Config *config, AppContext *app_context) {
  memset(app_context, 0, sizeof(AppContext));
  app_context->num_sources = config->num_sources;
  app_context->num_fans = config->num_fans;

  if (load_graph(config->graph_file, &app_context->graph) < 0) {
    (void)fprintf(stderr, "Error loading graph file: %s\n", config->graph_file);
    destroy_hardware(app_context);
    return -1;
  }

  app_context->sources = calloc(app_context->num_sources, sizeof(hwmonSource));
  if (app_context->sources == NULL) {
    perror("malloc for sources failed");
    destroy_hardware(app_context);
    return -1;
  }
  for (int i = 0; i < config->num_sources; i++) {
    if (hwmon_source_init(&config->sources[i], &app_context->sources[i]) < 0) {
      (void)fprintf(stderr, "Failed to initialise source: %s\n", config->sources[i].name);
      destroy_hardware(app_context);
      return -1;
    }
    app_context->initialised_sources++;
  }

  app_context->fans = calloc(config->num_fans, sizeof(hwmonFan));
  if (app_context->fans == NULL) {
    perror("malloc for fans failed");
    destroy_hardware(app_context);
    return -1;
  }
  for (int i = 0; i < config->num_fans; i++) {
    if (hwmon_fan_init(&config->fans[i], &app_context->fans[i]) < 0) {
      (void)fprintf(stderr, "Failed to initialise fan: %s\n", config->fans[i].name);
      destroy_hardware(app_context);
      return -1;
    }
    app_context->initialised_fans++;
  }

  return 0;
}

void run_main_loop(AppContext *app_context, Config *config) {
  long nanoseconds = (long)config->interval * MILLISECOND;
  struct timespec interval = { .tv_nsec = nanoseconds, .tv_sec = 0 };

  while (keep_running) {
    float highest_temp = get_highest_temp(app_context);
    float temp_average = moving_average_update(app_context, highest_temp);

    float target_fan_percent = calculate_fan_percent(app_context, temp_average);

    for (int i = 0; i < app_context->num_fans; i++) {
      int pwm_value = calculate_pwm_value(target_fan_percent, config->fans[i].min_pwm, config->fans[i].max_pwm);
      if (pwm_value != app_context->fans[i].last_pwm_value) {
        hwmon_set_pwm(&app_context->fans[i], pwm_value);
        app_context->fans[i].last_pwm_value = pwm_value;
      }
    }

    if (app_context->debug) {
      printf("\033[2J\033[Hhighest_temp:       %f\n", highest_temp);
                   printf("temp_average:       %f\n", temp_average);
                   printf("target_fan_percent: %f\n", target_fan_percent);
      for (int i = 0; i < app_context->num_fans; i++) {
                   printf("target_pwm_value:   %i\n", calculate_pwm_value(target_fan_percent, config->fans[i].min_pwm, config->fans[i].max_pwm));
                   printf("last_pwm_value      %i\n", app_context->fans[i].last_pwm_value);
      }
      if (fflush(stdout) == EOF) {
        perror("fflush");
      }
    }

    nanosleep(&interval, NULL);
  }
}

int main(int argc, char *argv[]) {

  if (signal(SIGINT, signal_handler) == SIG_ERR) {
    perror("signal");
  }
  if (signal(SIGTERM, signal_handler) == SIG_ERR) {
    perror("signal");
  }

  char *config_path = "/etc/cfans/config.ini";

  int opt;
  while ((opt = getopt(argc, argv, "c:")) != -1) {
    switch (opt) {
      case 'c':
        config_path = optarg;
        break;
      default:
        (void)fprintf(stderr, "Usage: %s [-c CONFIG_FILE]\n", argv[0]);
        return EXIT_FAILURE;
    } 
  }

  Config config;
  if (load_config(config_path, &config) < 0) {
    (void)fprintf(stderr, "Error loading config file: %s\n", config_path);
    return EXIT_FAILURE;
  }

  AppContext app_context;
  if (init_hardware(&config, &app_context) < 0) {
    (void)fprintf(stderr, "Failed to initialise hardware\n");
    free_config(&config);
    return EXIT_FAILURE;
  }

  if (moving_average_init(&app_context, config.average) < 0) {
    (void)fprintf(stderr, "Failed to initialise average buffer\n");
    return EXIT_FAILURE;
  }

  if (getenv("DEBUG") != NULL) {
    app_context.debug = true;
  }
  else {
    app_context.debug = false;
  }

  run_main_loop(&app_context, &config);

  // Restore automatic fan control
  for (int i = 0; i < app_context.num_fans; i++) {
    hwmon_restore_auto_control(&app_context.fans[i]);
  }
  destroy_hardware(&app_context);
  free_config(&config);

  return EXIT_SUCCESS;
}
