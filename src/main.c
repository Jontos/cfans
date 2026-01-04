#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
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

  if (app_context->source) {
    hwmon_source_destroy(app_context->source, app_context->initialised_sources);
  }
  if (app_context->fan) {
    hwmon_fan_destroy(app_context->fan, app_context->initialised_fans);
  }

  free(app_context->source);
  free(app_context->fan);
  free(app_context->average_buffer.slot);
}

int init_hardware(struct config *config, AppContext *app_context) {
  memset(app_context, 0, sizeof(AppContext));
  app_context->num_sources = config->num_sources;
  app_context->num_fans = config->num_fans;

  app_context->source = calloc(app_context->num_sources, sizeof(struct hwmon_source));
  if (app_context->source == NULL) {
    perror("malloc for sources failed");
    destroy_hardware(app_context);
    return -1;
  }
  for (int i = 0; i < config->num_sources; i++) {
    if (hwmon_source_init(&config->source[i], &app_context->source[i]) < 0) {
      (void)fprintf(stderr, "Failed to initialise source: %s\n", config->source[i].name);
      destroy_hardware(app_context);
      return -1;
    }
    app_context->initialised_sources++;
  }

  app_context->fan = calloc(config->num_fans, sizeof(struct hwmon_fan));
  if (app_context->fan == NULL) {
    perror("malloc for fans failed");
    destroy_hardware(app_context);
    return -1;
  }
  for (int i = 0; i < config->num_fans; i++) {
    if (hwmon_fan_init(&config->fan[i], &app_context->fan[i]) < 0) {
      (void)fprintf(stderr, "Failed to initialise fan: %s\n", config->fan[i].name);
      destroy_hardware(app_context);
      return -1;
    }
    app_context->initialised_fans++;
  }

  return 0;
}

void run_main_loop(AppContext *app_context, struct config *config) {
  long nanoseconds = (long)config->interval * MILLISECOND;
  struct timespec interval = { .tv_nsec = nanoseconds, .tv_sec = 0 };

  while (keep_running) {
    float highest_temp = get_highest_temp(app_context);
    float temp_average = moving_average_update(app_context, highest_temp);

    for (int i = 0; i < app_context->num_fans; i++) {
      app_context->fan[i].target_fan_percent = calculate_fan_percent(config->fan[i].curve, config->fan[i].num_points, temp_average);
      app_context->fan[i].target_pwm_value = calculate_pwm_value(app_context->fan[i].target_fan_percent, config->fan[i].min_pwm, config->fan[i].max_pwm);
      if (app_context->fan[i].target_pwm_value != app_context->fan[i].last_pwm_value) {
        hwmon_set_pwm(&app_context->fan[i], app_context->fan[i].target_pwm_value);
        app_context->fan[i].last_pwm_value = app_context->fan[i].target_pwm_value;
      }
    }

    if (app_context->debug) {
      printf("\033[2J\033[Hhighest_temp:         %f\n", highest_temp);
                   printf("hottest_device:       %s\n", app_context->hottest_device);
                   printf("hottest_sensor:       %s\n", app_context->source[app_context->hottest_device_index].hottest_sensor);
                   printf("temp_average:         %f\n", temp_average);
      for (int i = 0; i < app_context->num_fans; i++) {
                   printf("\n");
                   printf("%s:\n", config->fan[i].name);
                   printf("  target_fan_percent: %f\n", app_context->fan[i].target_fan_percent);
                   printf("  target_pwm_value:   %i\n", app_context->fan[i].target_pwm_value);
                   printf("  last_pwm_value      %i\n", app_context->fan[i].last_pwm_value);
      }
      if (fflush(stdout) == EOF) {
        perror("fflush");
      }
    }

    nanosleep(&interval, NULL);
  }
}

int main(int argc, char *argv[])
{
  struct sigaction sigact = {
    .sa_handler = signal_handler,
    .sa_flags = SA_RESTART
  };
  sigemptyset(&sigact.sa_mask);

  if (sigaction(SIGINT, &sigact, NULL) == -1) {
    perror("signal");
  }
  if (sigaction(SIGTERM, &sigact, NULL) == -1) {
    perror("signal");
  }

  const char *config_path = "/etc/cfans/config.ini";

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

  struct config config;
  if (load_config(config_path, &config) < 0) {
    (void)fprintf(stderr, "Error loading config file: %s\n", config_path);
    free_config(&config);
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
    hwmon_restore_auto_control(&app_context.fan[i]);
  }
  destroy_hardware(&app_context);
  free_config(&config);

  return EXIT_SUCCESS;
}
