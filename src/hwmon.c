#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-device.h>

#include "hwmon.h"

#define HWMON_FILENAME_BUFFER_SIZE 32
#define HWMON_MAX_PWM_VALUE 4

int hwmon_device_init(sd_device_enumerator **enumerator, sd_device **device,
                      const char *driver, const char *pci_device)
{
  *enumerator = NULL;
  sd_device_enumerator *parent_enumerator = NULL;
  sd_device *parent_device = NULL;

  int ret = sd_device_enumerator_new(enumerator);
  if (ret < 0) {
    (void)fprintf(stderr, "failed to create enumerator: %s\n", strerror(-ret));
    return -1;
  }

  if (pci_device) {
    ret = sd_device_enumerator_new(&parent_enumerator);
    if (ret < 0) {
      (void)fprintf(stderr, "failed to create enumerator: %s\n", strerror(-ret));
      goto failure;
    }

    ret = sd_device_enumerator_add_match_sysattr(parent_enumerator, "device", pci_device, 1);
    if (ret < 0) {
      (void)fprintf(stderr, "failed to add sysattr match: %s\n", strerror(-ret));
      goto failure;
    }

    parent_device = sd_device_enumerator_get_device_first(parent_enumerator);

    ret = sd_device_enumerator_add_match_parent(*enumerator, parent_device);
    if (ret < 0) {
      (void)fprintf(stderr, "failed to add parent match: %s\n", strerror(-ret));
      goto failure;
    }
  }

  if (driver) {
    ret = sd_device_enumerator_add_match_sysattr(*enumerator, "name", driver, 1);
    if (ret < 0) {
      (void)fprintf(stderr, "failed to add sysattr match: %s\n", strerror(-ret));
      return -1;
    }
  }

  ret = sd_device_enumerator_add_match_subsystem(*enumerator, "hwmon", 1);
  if (ret < 0) {
    (void)fprintf(stderr, "failed to add subsystem match: %s\n", strerror(-ret));
    return -1;
  }

  *device = sd_device_enumerator_get_device_first(*enumerator);
  if (sd_device_enumerator_get_device_next(*enumerator)) {
    (void)fprintf(stderr, "Error: more than one match for %s\n", driver);
    return -1;
  }

  sd_device_enumerator_unref(parent_enumerator);

  return 0;

failure:
  sd_device_enumerator_unref(*enumerator);
  if (parent_enumerator != NULL) {
    sd_device_enumerator_unref(parent_enumerator);
  }
  return -1;
}

int register_temp_inputs(struct hwmon_source *source,
                         struct sensor sensor[], int num_sensors)
{
  source->temp_input = calloc(num_sensors, sizeof(struct temp_input));
  source->num_inputs = num_sensors;
  if (source->temp_input == NULL) {
    perror("calloc");
    return -1;
  }

  for (int i = 0; i < num_sensors; i++) {
    const char *sysattr;
    const char *value;

    for (sysattr = sd_device_get_sysattr_first(source->device);
    sysattr;
    sysattr = sd_device_get_sysattr_next(source->device)) {

      if (strncmp(sysattr, "temp", 4) != 0 || strstr(sysattr, "_label") == NULL) {
        continue;
      }

      if (sd_device_get_sysattr_value(source->device, sysattr, &value) >= 0) {
        if (strcmp(sensor[i].name, value) != 0) continue;

        char temp_input[HWMON_FILENAME_BUFFER_SIZE];
        int num = (int)strtol(sysattr + strlen("temp"), NULL, 0);
        int ret = snprintf(temp_input, sizeof(temp_input), "temp%i_input", num);
        if (ret < 0 || (size_t)ret >= sizeof(temp_input)) {
          perror("snprintf");
          return -1;
        }

        source->temp_input[i].name = strdup(value);
        source->temp_input[i].filename = strdup(temp_input);
        if (source->temp_input[i].name == NULL || source->temp_input[i].filename == NULL) {
          perror("strdup");
          return -1;
        }
        break;
      }
    }
  }

  return 0;
}

int hwmon_source_init(struct source *config, struct hwmon_source *source) {
  if (hwmon_device_init(&source->enumerator, &source->device, config->driver, config->pci_device) < 0) {
    return -1;
  }
  if (register_temp_inputs(source, config->sensor, config->num_sensors) < 0) {
    return -1;
  }
  source->scale = (float)config->scale;
  source->name = config->name;

  return 0;
}

int hwmon_fan_init(struct fan *config, struct hwmon_fan *fan) {
  if (hwmon_device_init(&fan->enumerator, &fan->device, config->driver, NULL) < 0) {
    return -1;
  }
  fan->pwm_file = config->pwm_file;
  asprintf(&fan->pwm_enable_file, "%s_enable", config->pwm_file);

  return 0;
}

float hwmon_read_temp(struct hwmon_source *source) {
  float highest_temp = 0;
  for (int i = 0; i < source->num_inputs; i++) {
    // We need to send a NULL value to libsystemd so it clears the cached sysattr value
    if (sd_device_set_sysattr_value(source->device, source->temp_input[i].filename, NULL) != 0) {
      (void)fprintf(stderr, "Error clearing libsystemd sysattr cache for %s\n", source->temp_input[i].filename);
    }
    const char *value;
    if (sd_device_get_sysattr_value(source->device, source->temp_input[i].filename, &value) >= 0) {
      float temp = strtof(value, NULL) / source->scale;
      if (temp > highest_temp) {
        highest_temp = temp;
        source->hottest_sensor = source->temp_input[i].name;
      }
    }
  }
  return highest_temp;
}

int hwmon_set_pwm(struct hwmon_fan *fan, int pwm_value) {
  if (sd_device_set_sysattr_valuef(fan->device, fan->pwm_file, "%i", pwm_value) != 0) {
    (void)fprintf(stderr, "Error setting PWM value for %s\n", fan->pwm_file);
    return -1;
  }

  return 0;
}

int hwmon_restore_auto_control(struct hwmon_fan *fan) {
  const char *pwm_auto_mode = "99";

  if (sd_device_set_sysattr_value(fan->device, fan->pwm_enable_file, pwm_auto_mode) >= 0) {
    return 0;
  }

  return -1;
}

void hwmon_source_destroy(struct hwmon_source *sources, int num_sources) {
  for (int i = 0; i < num_sources; i++) {
    sd_device_enumerator_unref(sources[i].enumerator);

    for (int j = 0; j < sources[i].num_inputs; j++) {
      free(sources[i].temp_input[j].filename);
      free(sources[i].temp_input[j].name);
    }
    free(sources[i].temp_input);
  }
}

void hwmon_fan_destroy(struct hwmon_fan *fans, int num_fans) {
  for (int i = 0; i < num_fans; i++) {
    sd_device_enumerator_unref(fans[i].enumerator);
    free(fans[i].pwm_enable_file);
  }
}
