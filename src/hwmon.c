#include "hwmon.h"
#include <dirent.h>
#include <errno.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config_parser.h"
#include "utils.h"

#define HWMON_FILENAME_BUFFER_SIZE 32
#define PWM_MODE_AUTO "99"

char *find_hwmon_path(const char *driver, const char *pci_device) {
  char *hwmon_parent_dir = "/sys/class/hwmon";
  DIR *dirp = opendir(hwmon_parent_dir);
  if (dirp == NULL) {
    (void)fprintf(stderr, "Failed to open %s: %s\n", hwmon_parent_dir, strerror(errno));
    return NULL;
  }

  char path_buffer[PATH_MAX];
  char *hwmon_path = NULL;
  while (true) {
    errno = 0;
    struct dirent *direntp = readdir(dirp);

    if (errno > 0) {
      (void)fprintf(stderr, "Failed to read directory %s: %s\n", hwmon_parent_dir, strerror(errno));
      return NULL;
    }
    if (direntp == NULL) {
      break;
    }
    if (direntp->d_name[0] == '.') {
      continue;
    }

    int ret = snprintf(path_buffer, sizeof(path_buffer), "%s/%s/name", hwmon_parent_dir, direntp->d_name);
    if (ret < 0 || (size_t)ret >= sizeof(path_buffer)) {
      perror("snprintf");
      return NULL;
    }

    char *driver_name = line_from_file(path_buffer);
    if (strcmp(driver_name, driver) != 0) {
      free(driver_name);
      continue;
    }
    free(driver_name);

    if (pci_device != NULL) {
      ret = snprintf(path_buffer, sizeof(path_buffer), "%s/%s/device/device", hwmon_parent_dir, direntp->d_name);
      if (ret < 0 || (size_t)ret >= sizeof(path_buffer)) {
        perror("snprintf");
        return NULL;
      }
      char *device_name = line_from_file(path_buffer);
      if (strstr(device_name, pci_device) == NULL) {
        free(device_name);
        continue;
      }
      free(device_name);
    }

    if (sprintf(path_buffer, "%s/%s", hwmon_parent_dir, direntp->d_name) < 0) {
      perror("sprintf");
      return NULL;
    }
    hwmon_path = strdup(path_buffer);
    break;
  }
  closedir(dirp);
  return hwmon_path;
}

int register_temp_inputs(hwmonSource *source, const char *hwmon_path, const char *sensors_string) {
  DIR *dirp = opendir(hwmon_path);
  if (dirp == NULL) {
    (void)fprintf(stderr, "Failed to open %s: %s\n", hwmon_path, strerror(errno));
    return -1;
  }

  char path_buffer[PATH_MAX];
  while (1) {
    errno = 0;
    struct dirent *direntp = readdir(dirp);

    if (errno > 0) {
      (void)fprintf(stderr, "Failed to read directory %s: %s\n", hwmon_path, strerror(errno));
      return -1;
    }
    if (direntp == NULL) {
      break;
    }
    if (strncmp(direntp->d_name, "temp", 4) != 0 || strstr(direntp->d_name, "_label") == NULL) {
      continue;
    }

    int ret = snprintf(path_buffer, sizeof(path_buffer), "%s/%s", hwmon_path, direntp->d_name);
    if (ret < 0 || (size_t)ret >= sizeof(path_buffer)) {
      perror("snprintf");
      return -1;
    }

    char *label = line_from_file(path_buffer);
    if (strstr(sensors_string, label) == NULL) {
      free(label);
      continue;
    }
    free(label);

    char temp_input_file[HWMON_FILENAME_BUFFER_SIZE];
    int num = (int)strtol(direntp->d_name + strlen("temp"), NULL, 0);
    ret = snprintf(temp_input_file, sizeof(temp_input_file), "temp%i_input", num);
    if (ret < 0 || (size_t)ret >= sizeof(temp_input_file)) {
      perror("snprintf");
      return -1;
    }
    char *temp_input_path = concat_string(hwmon_path, temp_input_file, '/');

    if (source->num_inputs == source->input_capacity) {
      if (resize_array((void**)&source->temp_inputs, sizeof(char*), &source->input_capacity) < 0) {
        return -1;
      }
    }

    source->temp_inputs[source->num_inputs] = temp_input_path;
    source->num_inputs++;
  }
  closedir(dirp);
  return 0;
}

int hwmon_pwm_enable(hwmonFan fan, int mode) {
  int status = -1;

  if (seteuid(0) < 0) {
    perror("seteuid(0) failed");
    return -1;
  }

  FILE *file = fopen(fan.pwm_enable_file_path, "r+");
  if (file) {
    char *flag = "1";
    if (!mode) {
      flag = PWM_MODE_AUTO;
    }
    if (fputs(flag, file) >= 0) {
      status = 0;
    }
    else {
      (void)fprintf(stderr, "Failed to write to %s: %s\n", fan.pwm_enable_file_path, strerror(errno));
    }
    if (fclose(file) == EOF) {
      perror("fclose");
    }
  }
  else {
    (void)fprintf(stderr, "Failed to open %s: %s\n", fan.pwm_enable_file_path, strerror(errno));
  }

  if (seteuid(getuid()) < 0) {
    perror("seteuid(getuid) failed");
    exit(EXIT_FAILURE);
  }

  return status;
}

int hwmon_read_temp(hwmonSource source, int scale) {
  int highest_temp = 0;
  for (int i = 0; i < source.num_inputs; i++) {
    char *temperature_string = line_from_file(source.temp_inputs[i]);
    int temp = (int)strtol(temperature_string, NULL, 0) / scale;
    free(temperature_string);

    if (temp > highest_temp) {
      highest_temp = temp;
    }
  }
  return highest_temp;
}

int hwmon_set_pwm(hwmonFan fan, int value) {
  int status = -1;

  if (seteuid(0) < 0) {
    perror("seteuid(0) failed");
    return -1;
  }

  FILE *file = fopen(fan.pwm_file_path, "w");
  if (file) {
    if (fprintf(file, "%i", value) >= 0) {
      status = 0;
    }
    else {
      (void)fprintf(stderr, "Failed to write to %s: %s\n", fan.pwm_file_path, strerror(errno));
    }
    if (fclose(file) == EOF) {
      perror("fclose");
    }
  }
  else {
    (void)fprintf(stderr, "Failed to open %s: %s\n", fan.pwm_file_path, strerror(errno));
  }

  if (seteuid(getuid()) < 0) {
    perror("seteuid(getuid) failed");
    exit(EXIT_FAILURE);
  }

  return status;
}

int hwmon_fan_init(Fan config, hwmonFan *fan) {
  char *hwmon_path = find_hwmon_path(config.driver, NULL);
  fan->pwm_file_path = concat_string(hwmon_path, config.pwm_file, '/');
  free(hwmon_path);
  fan->pwm_enable_file_path = concat_string(fan->pwm_file_path, "enable", '_');
  if (fan->pwm_file_path == NULL || fan->pwm_enable_file_path == NULL) {
    return -1;
  }
  return 0;
}

int hwmon_source_init(Source config, hwmonSource *source) {
  char *hwmon_path = find_hwmon_path(config.driver, config.pci_device);
  if (register_temp_inputs(source, hwmon_path, config.sensors_string) < 0) {
    free(hwmon_path);
    return -1;
  }
  free(hwmon_path);
  source->scale = config.scale;
  return 0;
}

void hwmon_fan_destroy(hwmonFan *fans, const int num_fans) {
  for (int i = 0; i < num_fans; i++) {
    free(fans[i].pwm_file_path);
    free(fans[i].pwm_enable_file_path);
  }
}

void hwmon_source_destroy(hwmonSource *sources, const int num_sources) {
  for (int i = 0; i < num_sources; i++) {
    for (int j = 0; j < sources[i].num_inputs; j++) {
      free(sources[i].temp_inputs[j]);
    }
    free((void*)sources[i].temp_inputs);
  }
}
