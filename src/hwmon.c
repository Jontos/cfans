#include <stdio.h>
#include <stdlib.h>
#include <systemd/sd-device.h>

#include "hwmon.h"
#include "utils.h"
#include "config_parser.h"

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

int register_temp_inputs(hwmonSource *source, const char *sensors_string) {
  const char *sysattr;
  const char *value;

  for (sysattr = sd_device_get_sysattr_first(source->device);
  sysattr;
  sysattr = sd_device_get_sysattr_next(source->device)) {

    if (strncmp(sysattr, "temp", 4) != 0 || strstr(sysattr, "_label") == NULL) {
      continue;
    }

    if (sd_device_get_sysattr_value(source->device, sysattr, &value) >= 0) {
      if (strstr(sensors_string, value) == NULL) {
        continue;
      }
      char temp_input[HWMON_FILENAME_BUFFER_SIZE];
      int num = (int)strtol(sysattr + strlen("temp"), NULL, 0);
      int ret = snprintf(temp_input, sizeof(temp_input), "temp%i_input", num);
      if (ret < 0 || (size_t)ret >= sizeof(temp_input)) {
        perror("snprintf");
        return -1;
      }

      if (source->num_inputs == source->input_capacity) {
        if (resize_array((void**)&source->temp_inputs, sizeof(char*), &source->input_capacity) < 0) {
          return -1;
        }
      }

      source->temp_inputs[source->num_inputs] = strdup(temp_input);
      source->num_inputs++;
    }
  }

  return 0;
}

int hwmon_source_init(Source *config, hwmonSource *source) {
  if (hwmon_device_init(&source->enumerator, &source->device, config->driver, config->pci_device) < 0) {
    return -1;
  }
  if (register_temp_inputs(source, config->sensors_string) < 0) {
    return -1;
  }
  source->scale = config->scale;

  return 0;
}

int hwmon_fan_init(Fan *config, hwmonFan *fan) {
  if (hwmon_device_init(&fan->enumerator, &fan->device, config->driver, NULL) < 0) {
    return -1;
  }
  fan->pwm_file = config->pwm_file;
  fan->pwm_enable_file = concat_string(config->pwm_file, "enable", '_');

  return 0;
}

float hwmon_read_temp(hwmonSource *source) {
  float highest_temp = 0;
  for (int i = 0; i < source->num_inputs; i++) {
    // We need to send a NULL value to libsystemd so it clears the cached sysattr value
    if (sd_device_set_sysattr_value(source->device, source->temp_inputs[i], NULL) != 0) {
      (void)fprintf(stderr, "Error clearing libsystemd sysattr cache for %s\n", source->temp_inputs[i]);
    }
    const char *value;
    if (sd_device_get_sysattr_value(source->device, source->temp_inputs[i], &value) >= 0) {
      float temp = strtof(value, NULL) / source->scale;
      if (temp > highest_temp) {
        highest_temp = temp;
      }
    }
  }
  return highest_temp;
}

int hwmon_set_pwm(hwmonFan *fan, int pwm_value) {
  char value[HWMON_MAX_PWM_VALUE];
  int ret = snprintf(value, sizeof(value), "%i", pwm_value);
  if (ret < 0 || (size_t)ret >= sizeof(value)) {
    perror("snprintf");
  }

  if (sd_device_set_sysattr_value(fan->device, fan->pwm_file, value) != 0) {
    (void)fprintf(stderr, "Error setting PWM value for %s\n", fan->pwm_file);
    return -1;
  }

  return 0;
}

int hwmon_restore_auto_control(hwmonFan *fan) {
  const char *pwm_auto_mode = "99";

  if (sd_device_set_sysattr_value(fan->device, fan->pwm_enable_file, pwm_auto_mode) >= 0) {
    return 0;
  }

  return -1;
}

void hwmon_source_destroy(hwmonSource *sources, int num_sources) {
  for (int i = 0; i < num_sources; i++) {
    sd_device_enumerator_unref(sources[i].enumerator);

    for (int j = 0; j < sources[i].num_inputs; j++) {
      free(sources[i].temp_inputs[j]);
    }
    free((void*)sources[i].temp_inputs);
  }
}

void hwmon_fan_destroy(hwmonFan *fans, int num_fans) {
  for (int i = 0; i < num_fans; i++) {
    sd_device_enumerator_unref(fans[i].enumerator);
    free(fans[i].pwm_enable_file);
  }
}
