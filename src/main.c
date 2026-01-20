#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
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

void destroy_hardware(struct app_context *app_context)
{
  hwmon_destroy_sources(app_context);
  hwmon_destroy_fans(app_context);
}

void update_fans(struct app_fan fan[], int num_fans)
{
  for (int i = 0; i < num_fans; i++) {
    float temperature = fan[i].curve->sensor->get_temp_func(fan[i].curve->sensor->data);
    float fan_percent = calculate_fan_percent(fan[i].config->curve, temperature);
    int pwm_value = calculate_pwm_value(fan_percent, fan[i].config->min_pwm, fan[i].config->max_pwm, fan[i].config->zero_rpm);
    hwmon_set_pwm(fan[i].hwmon, pwm_value);
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

  const char *config_path = "/etc/cfans/config.json";

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

  struct config config = {0};
  if (load_config(config_path, &config) < 0) {
    (void)fprintf(stderr, "Error loading config file: %s\n", config_path);
    free_config(&config);
    return EXIT_FAILURE;
  }

  struct app_context app_context = {0};
  if (hwmon_init_sources(&config, &app_context) < 0 ||
      hwmon_init_fans(&config, &app_context) < 0)
  {
    (void)fprintf(stderr, "Failed to initialise hardware\n");
    destroy_hardware(&app_context);
    free_config(&config);
    return EXIT_FAILURE;
  }

  if (getenv("DEBUG") != NULL) {
    app_context.debug = true;
  }
  else {
    app_context.debug = false;
  }

  long nanoseconds = config.interval * MILLISECOND;
  struct timespec interval = { .tv_nsec = nanoseconds, .tv_sec = 0 };
  while (keep_running) {
    update_fans(app_context.fan, app_context.num_fans);
    nanosleep(&interval, NULL);
  }

  // Restore automatic fan control
  for (int i = 0; i < app_context.num_fans; i++) {
    hwmon_restore_auto_control(app_context.fan[i].hwmon);
  }
  destroy_hardware(&app_context);
  free_config(&config);

  return EXIT_SUCCESS;
}
