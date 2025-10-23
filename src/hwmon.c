#include "hwmon.h"
#include <dirent.h>
#include <errno.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config_parser.h"
#include "utils.h"

char *find_hwmon_path(const char *driver, const char *pci_device) {
  char *hwmon_path = "/sys/class/hwmon";
  DIR *dirp = opendir(hwmon_path);
  if (dirp == NULL) {
    (void)fprintf(stderr, "Failed to open %s: %s\n", hwmon_path, strerror(errno));
    return NULL;
  }

  while (true) {
    errno = 0;
    struct dirent *direntp = readdir(dirp);

    if (errno > 0) {
      (void)fprintf(stderr, "Failed to read directory %s: %s\n", hwmon_path, strerror(errno));
      return NULL;
    }
    if (direntp == NULL) {
      break;
    }
    if (direntp->d_name[0] == '.') {
      continue;
    }

    char *hwmon_dir = concat_string(hwmon_path, direntp->d_name, '/');
    char *name_file = concat_string(hwmon_dir, "name", '/');
    if (hwmon_dir == NULL || name_file == NULL) {
      (void)fprintf(stderr, "Error creating path for %s\n", direntp->d_name);
      continue;
    }

    char *driver_name = line_from_file(name_file);
    if (strcmp(driver_name, driver) != 0) {
      continue;
    }
    if (pci_device != NULL) {
      char *device_file = concat_string(hwmon_dir, "device/device", '/');
      if (device_file == NULL) {
        (void)fprintf(stderr, "Error creating path for %s/device/device\n", direntp->d_name);
        continue;
      }
      char *device_name = line_from_file(device_file);
      if (strstr(device_name, pci_device) == NULL) {
        continue;
      }
    }
    closedir(dirp);

    return hwmon_dir;
  }
  closedir(dirp);
  return NULL;
}

int register_temp_inputs(hwmonSource *source, const char *hwmon_path, const char *sensors_string) {
  DIR *dirp = opendir(hwmon_path);
  if (dirp == NULL) {
    (void)fprintf(stderr, "Failed to open %s: %s\n", hwmon_path, strerror(errno));
    return -1;
  }

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

    char *label_file = concat_string(hwmon_path, direntp->d_name, '/');
    char *label = line_from_file(label_file);
    if (strstr(sensors_string, label) == NULL) {
      continue;
    }
    char *delimiter = strchr(direntp->d_name, '_');
    delimiter[0] = '\0';
    char *temp_input_file = concat_string(direntp->d_name, "input", '_');
    char *temp_input_file_path = concat_string(hwmon_path, temp_input_file, '/');

    if (source->num_inputs == source->input_capacity) {
      if (resize_array((void**)&source->temp_inputs, sizeof(char*), &source->input_capacity) < 0) {
        return -1;
      }
    }

    source->temp_inputs[source->num_inputs] = temp_input_file_path;
    source->num_inputs++;
  }
  closedir(dirp);
  return 0;
}

int hwmon_pwm_enable(hwmonFan fan, int mode) {
  FILE *file = fopen(fan.pwm_enable_file, "r+");
  if (file == NULL) {
    (void)fprintf(stderr, "Failed to open %s: %s\n", fan.pwm_enable_file, strerror(errno));
    return -1;
  }

  char *flag = "1";
  if (!mode) {
    flag = "99";
  }

  if (fputs(flag, file) == EOF) {
    (void)fprintf(stderr, "Failed to write to %s: %s\n", fan.pwm_enable_file, strerror(errno));
    if (fclose(file) == EOF) {
      perror("fclose");
    }
    return -1;
  }

  if (fclose(file) == EOF) {
    perror("fclose");
  }
  return 0;
}

int hwmon_read_temp(hwmonSource source, int *temperatures, int scale) {
  for (int i = 0; i < source.num_inputs; i++) {
    char *temperature_string = line_from_file(source.temp_inputs[i]);
    temperatures[i] = (int)strtol(temperature_string, NULL, 0) / scale;
  }
  return 0;
}

int hwmon_set_pwm(hwmonFan fan, int value) {
  FILE *file = fopen(fan.pwm_file, "w");
  if (file == NULL) {
    (void)fprintf(stderr, "Failed to open %s: %s\n", fan.pwm_file, strerror(errno));
    return -1;
  }

  if (fprintf(file, "%i", value) < 0) {
    (void)fprintf(stderr, "Failed to write to %s: %s\n", fan.pwm_file, strerror(errno));
    return -1;
  }

  if (fclose(file) == EOF) {
    perror("fclose");
  }

  return 0;
}

hwmonFan hwmon_fan_init(Fan fan) {
  hwmonFan hwmon_fan = {
    .pwm_file = concat_string(find_hwmon_path(fan.driver, NULL), fan.pwm_file, '/'),
    .pwm_enable_file = concat_string(hwmon_fan.pwm_file, "enable", '_')
  };
  return hwmon_fan;
}

hwmonSource hwmon_source_init(Source source) {
  hwmonSource hwmon_source = {
    .temp_inputs = NULL,
    .num_inputs = 0,
    .input_capacity = 0
  };
  const char *hwmon_path = find_hwmon_path(source.driver, source.pci_device);
  register_temp_inputs(&hwmon_source, hwmon_path, source.sensors_string);

  return hwmon_source;
}
