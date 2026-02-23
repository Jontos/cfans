#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-device.h>
#include <fcntl.h>
#include <unistd.h>

#include "hwmon.h"
#include "control.h"
#include "config.h"

#define HWMON_FILENAME_BUFFER_SIZE 32
#define HWMON_MAX_PWM_VALUE 16
#define TEMP_INPUT_SIZE 32

static sd_device *get_sd_device(const char *device_id)
{
  sd_device_enumerator *enumerator [[gnu::cleanup(sd_device_enumerator_unrefp)]] = NULL;
  sd_device *parent [[gnu::cleanup(sd_device_unrefp)]] = NULL;

  int ret = sd_device_new_from_device_id(&parent, device_id);
  if (ret < 0) {
    (void)fprintf(stderr, "failed to find device ID \"%s\": %s\n", device_id, strerror(-ret));
    return NULL;
  }

  ret = sd_device_enumerator_new(&enumerator);
  if (ret < 0) {
    (void)fprintf(stderr, "failed to create enumerator: %s\n", strerror(-ret));
    return NULL;
  }

  ret = sd_device_enumerator_add_match_parent(enumerator, parent);
  if (ret < 0) {
    (void)fprintf(stderr, "failed to add parent match: %s\n", strerror(-ret));
    return NULL;
  }

  ret = sd_device_enumerator_add_match_subsystem(enumerator, "hwmon", 1);
  if (ret < 0) {
    (void)fprintf(stderr, "failed to add subsystem match: %s\n", strerror(-ret));
    return NULL;
  }

  sd_device *child = sd_device_enumerator_get_device_first(enumerator);
  if (child == NULL) {
    (void)fprintf(stderr, "Error: no hwmon device for PCI device \"%s\"\n", device_id);
    return NULL;
  }

  const char *devpath;
  ret = sd_device_get_devpath(child, &devpath);
  if (ret < 0) {
    (void)fprintf(stderr, "failed to get hwmon devpath for PCI device \"%s\": %s\n", device_id, strerror(-ret));
    return NULL;
  }

  return sd_device_ref(child);
}

static int init_sensors(sd_device *device,
                         struct source_config *source_config,
                         struct app_context *app_context)
{
  const char *syspath;
  int ret = sd_device_get_syspath(device, &syspath);
  if (ret < 0) {
    (void)fprintf(stderr, "Failed to get path for \"%s\": %s\n", source_config->device_id, strerror(-ret));
    return -1;
  }

  int count = 0;
  for (const char *sysattr = sd_device_get_sysattr_first(device);
       sysattr;
       sysattr = sd_device_get_sysattr_next(device))
  {
    if (count >= source_config->num_sensors) break;

    if (strncmp(sysattr, "temp", 4) != 0 || strstr(sysattr, "_label") == NULL) {
      continue;
    }

    const char *value;
    ret = sd_device_get_sysattr_value(device, sysattr, &value);
    if (ret < 0) {
      (void)fprintf(stderr, "Failed to read \"%s\": %s\n", sysattr, strerror(-ret));
      continue;
    }

    for (int i = 0; i < source_config->num_sensors; i++) {
      if (strcmp(source_config->sensor[i].name, value) == 0) {
        struct hwmon_sensor *sensor = malloc(sizeof(struct hwmon_sensor));

        long num = strtol(sysattr + strlen("temp"), NULL, 0);
        char *temp_input_path;
        if (asprintf(&temp_input_path, "%s/temp%li_input", syspath, num) < 0) {
          perror("asprintf");
          return -1;
        }

        sensor->fildes = open(temp_input_path, O_RDONLY);
        if (sensor->fildes < 0) {
          (void)fprintf(stderr, "Failed to open %s: %s\n", temp_input_path, strerror(errno));
          free(temp_input_path);
          free(sensor);
          return -1;
        }
        free(temp_input_path);

        sensor->scale = 0;
        sensor->offset = source_config->sensor[i].offset;

        app_context->sensor[app_context->num_sensors].name = source_config->sensor[i].name;
        app_context->sensor[app_context->num_sensors].config = &source_config->sensor[i];
        app_context->sensor[app_context->num_sensors].sensor_data = sensor;
        app_context->sensor[app_context->num_sensors].get_temp_func = hwmon_read_temp;
        app_context->num_sensors++;
        app_context->num_hwmon_sensors++;

        count++;
        break;
      }
    }

  }

  return 0;
}

int hwmon_init_sources(struct config *config, struct app_context *app_context)
{
  int sensor_count = 0;
  for (int i = 0; i < config->num_sources; i++) {
    sensor_count += config->source[i].num_sensors;
  }

  app_context->sensor = calloc(sensor_count, sizeof(struct app_sensor));
  if (app_context->sensor == NULL) {
    perror("Failed to allocate app_sensor array");
    return -1;
  }

  for (int i = 0; i < config->num_sources; i++) {
    sd_device *device [[gnu::cleanup(sd_device_unrefp)]] = NULL;
    device = get_sd_device(config->source[i].device_id);
    if (device == NULL) {
      return -1;
    }
    if (init_sensors(device, &config->source[i], app_context) < 0) {
      return -1;
    }
  }

  return 0;
}

static int init_fan(const char *syspath, struct fan_config *config, struct hwmon_fan *fan)
{
  char *pwm_file;
  if (asprintf(&pwm_file, "%s/%s", syspath, config->pwm_file) < 0 ||
    asprintf(&fan->pwm_enable_file, "%s_enable", config->pwm_file) < 0)
  {
    perror("asprintf");
    return -1;
  }

  fan->pwm_fildes = open(pwm_file, O_WRONLY);
  if (fan->pwm_fildes < 0) {
    (void)fprintf(stderr, "Failed to open %s: %s\n", pwm_file, strerror(errno));
    free(pwm_file);
    return -1;
  }
  free(pwm_file);

  if (sd_device_get_sysattr_value(fan->device, fan->pwm_enable_file, &fan->pwm_auto_control) < 0) {
    (void)fprintf(stderr, "Failed to read %s\n", fan->pwm_enable_file);
    return -1;
  }

  return 0;
}

int hwmon_init_fans(struct config *config, struct app_context *app_context)
{
  app_context->fan = calloc(config->num_fans, sizeof(struct app_fan));

  for (int i = 0; i < config->num_fans; i++) {
    app_context->fan[i].hwmon = malloc(sizeof(struct hwmon_fan));
    if (!app_context->fan[i].hwmon ) {
      perror("Failed to allocate hwmon_fan struct");
      return -1;
    }

    app_context->fan[i].hwmon->device = get_sd_device(config->fan[i].device_id);
    if (!app_context->fan[i].hwmon->device) return -1;

    const char *syspath;
    int ret = sd_device_get_syspath(app_context->fan[i].hwmon->device, &syspath);
    if (ret < 0) {
      (void)fprintf(stderr, "Failed to get path for \"%s\": %s\n",
                    config->fan[i].device_id, strerror(-ret));
      return -1;
    }

    if (init_fan(syspath, &config->fan[i], app_context->fan[i].hwmon) < 0) return -1;

    app_context->fan[i].config = &config->fan[i];
    app_context->fan[i].curve = calloc(1, sizeof(struct app_curve));
    if (!app_context->fan[i].curve) {
      perror("Failed to allocate app_curve");
      return -1;
    }

    app_context->fan[i].curve->config = config->fan[i].curve;
    app_context->num_fans++;
  }

  return 0;
}

int hwmon_read_temp(struct app_sensor *app_sensor)
{
  struct hwmon_sensor *sensor = app_sensor->sensor_data;

  char temp_input_string[TEMP_INPUT_SIZE];
  ssize_t nread = pread(sensor->fildes, temp_input_string, TEMP_INPUT_SIZE - 1, 0);
  if (nread < 0) {
    perror("Couldn't read temperature");
    return -1;
  }
  
  temp_input_string[nread] = '\0';
  float temp = strtof(temp_input_string, NULL);

  if (sensor->scale == 0) {
    if (temp > MILLIDEGREES) {
      sensor->scale = MILLIDEGREES;
    }
    else {
      sensor->scale = DEGREES;
    }
  }

  app_sensor->current_value = temp / sensor->scale + sensor->offset;

  return 0;
}

int hwmon_set_pwm(struct hwmon_fan *fan, int pwm_value)
{
  char pwm_string[HWMON_MAX_PWM_VALUE];

  int len = snprintf(pwm_string, HWMON_MAX_PWM_VALUE, "%i", pwm_value);

  if (pwrite(fan->pwm_fildes, pwm_string, len, 0) < 0) {
    perror("Couldn't change pwm value");
    return -1;
  }

  return 0;
}

int hwmon_restore_auto_control(struct hwmon_fan *fan)
{
  int ret = sd_device_set_sysattr_value(fan->device, fan->pwm_enable_file, fan->pwm_auto_control);
  if (ret < 0) {
    (void)fprintf(stderr, "Failed to set %s back to auto control: %s\n",
                  fan->pwm_enable_file, strerror(-ret));
    return -1;
  }

  return 0;
}

void hwmon_destroy_sources(struct app_context *app_context)
{
  for (int i = 0; i < app_context->num_hwmon_sensors; i++) {
    if (close(((struct hwmon_sensor*)app_context->sensor[i].sensor_data)->fildes) == -1) {
      perror("close");
    }
    free(app_context->sensor[i].sensor_data);
  }
}

void hwmon_destroy_fans(struct app_context *app_context)
{
  for (int i = 0; i < app_context->num_fans; i++) {
    sd_device_unref(app_context->fan[i].hwmon->device);
    if (close(app_context->fan[i].hwmon->pwm_fildes) == -1) {
      perror("close");
    }
    free(app_context->fan[i].hwmon->pwm_enable_file);
    free(app_context->fan[i].hwmon);
    free(app_context->fan[i].curve);
  }
  free(app_context->fan);
}
