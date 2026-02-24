#include <getopt.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#ifdef DEBUG
#include <ncurses.h>
#endif // DEBUG

#include "config.h"
#include "control.h"
#include "hwmon.h"

#define NANOSECOND (int)1e9
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

#ifdef DEBUG
void ui_update(struct app_context *ctx)
{
  erase();

  for (int i = 0; i < ctx->num_fans; i++) {
    struct app_fan *fan = &ctx->fan[i];

    // NOLINTBEGIN(readability-magic-numbers)
    mvprintw(i + 2, 0, "%s", fan->config->name);

    mvprintw(i + 2, 37, "%6.2fC", fan->curve->sensor->current_value);
    mvprintw(i + 2, 48, "%3.0f%%", fan->fan_percent);
    mvprintw(i + 2, 56, "%6.2fC", fan->curve->hyst_val);
    mvprintw(i + 2, 68, "%6.2fC", fan->curve->config->hysteresis);
    if (fan->curve->timer.tv_sec > 0) {
      if (clock_gettime(CLOCK_MONOTONIC, &ctx->clock) == -1) {
        perror("clock_gettime");
      }

      long elapsed = (ctx->clock.tv_sec - fan->curve->timer.tv_sec) +
                     (ctx->clock.tv_nsec - fan->curve->timer.tv_nsec) / NANOSECOND;
      long remaining = (long)fan->curve->config->response_time - elapsed;

      mvprintw(i + 2, 76, "%ld", remaining);
    }
    // NOLINTEND(readability-magic-numbers)
  }

  refresh();
}
#endif // DEBUG

void update_fans(struct app_fan fan[], int num_fans, struct timespec *clock)
{
  for (int i = 0; i < num_fans; i++) {
    if (fan[i].curve->sensor->get_temp_func(fan[i].curve->sensor) < 0) {
      (void)fprintf(stderr, "Failed to read temperature for %s\n", fan[i].curve->sensor->name);
    }

    if (fan[i].curve->config->hysteresis > 0) {
      if (fabsf(fan[i].curve->hyst_val - fan[i].curve->sensor->current_value) < fan[i].curve->config->hysteresis) {
        fan[i].curve->timer.tv_sec = 0;
        continue;
      }
    }

    if (fan[i].curve->config->response_time > 0) {
      if (clock_gettime(CLOCK_MONOTONIC, clock) == -1) {
        perror("clock_gettime");
      }

      if (fan[i].curve->timer.tv_sec == 0) {
        fan[i].curve->timer = *clock;
        continue;
      }

      long elapsed = (clock->tv_sec - fan[i].curve->timer.tv_sec) +
                     (clock->tv_nsec - fan[i].curve->timer.tv_nsec) / NANOSECOND;

      if ((long)fan[i].curve->config->response_time > elapsed) {
        continue;
      }
    }

    fan[i].curve->hyst_val = fan[i].curve->sensor->current_value;

    fan[i].fan_percent = calculate_fan_percent(fan[i].config->curve, fan[i].curve->sensor->current_value);
    int pwm_value = calculate_pwm_value(fan[i].fan_percent, fan[i].config);

    if (pwm_value != fan[i].pwm_value) {
      fan[i].pwm_value = pwm_value;
      if (hwmon_set_pwm(fan[i].hwmon, fan[i].pwm_value) < 0) {
        (void)fprintf(stderr, "Failed to set fan speed for %s\n", fan[i].config->name);
      }
    }

    fan[i].curve->timer.tv_sec = 0;
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
      hwmon_init_fans(&config, &app_context) < 0 ||
      init_custom_sensors(&config, &app_context) < 0 ||
      link_curve_sensors(&app_context) < 0)
  {
    (void)fprintf(stderr, "Failed to initialise hardware\n");
    destroy_hardware(&app_context);
    free_config(&config);
    return EXIT_FAILURE;
  }

#ifdef DEBUG
  initscr();
  cbreak();
  noecho();
#endif // DEBUG

  long nanoseconds = (long)config.interval * MILLISECOND;
  struct timespec interval = { .tv_nsec = nanoseconds, .tv_sec = 0 };
  while (keep_running) {
    update_fans(app_context.fan, app_context.num_fans, &app_context.clock);

#ifdef DEBUG
    ui_update(&app_context);
#endif // DEBUG

    nanosleep(&interval, NULL);
  }

  for (int i = 0; i < app_context.num_fans; i++) {
    hwmon_restore_auto_control(app_context.fan[i].hwmon);
  }
  destroy_hardware(&app_context);
  destroy_custom_sensors(&app_context);
  free_config(&config);

#ifdef DEBUG
  endwin();
#endif // DEBUG

  return EXIT_SUCCESS;
}
