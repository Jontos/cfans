#include "config.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int debug;

void get_dev_path(const char *value, char *dev_path, int size) {
  int found = 0;

  char *hwmon_path = "/sys/class/hwmon";
  DIR *dirp = opendir(hwmon_path);
  if (dirp == NULL) {
    (void)fprintf(stderr, "Failed to open %s: %s\n", hwmon_path, strerror(errno));
    exit(EXIT_FAILURE);
  }

  while (1) {
    errno = 0;
    struct dirent *direntp = readdir(dirp);

    if (direntp == NULL) {
      if (errno > 0) {
        (void)fprintf(stderr, "Failed to read directory %s: %s\n", hwmon_path, strerror(errno));
        exit(EXIT_FAILURE);
      }
      break;
    }

    if (direntp->d_name[0] == '.') {
      continue;
    }

    char namebuffer[PAGE_SIZE];
    char namefile[PATH_MAX];
    (void)snprintf(namefile, sizeof(namefile), "%s/%s/name", hwmon_path, direntp->d_name);

    FILE *filep = fopen(namefile, "r");
    if (filep == NULL) {
      (void)fprintf(stderr, "Failed to open %s: %s\n", namefile, strerror(errno));
      exit(EXIT_FAILURE);
    }

    if (fgets(namebuffer, sizeof(namebuffer), filep) == NULL) {
      (void)fprintf(stderr, "Failed to read %s: %s\n", namefile, strerror(errno));
      exit(EXIT_FAILURE);
    }
    if (fclose(filep) == EOF) {
      perror("fclose");
    }

    char *newline = strchr(namebuffer, '\n');
    if (newline) {
      newline[0] = '\0';
    }

    if (strcmp(namebuffer, value) == 0) {
      (void)snprintf(dev_path, size, "%s/%s", hwmon_path, direntp->d_name);
      found = 1;
      break;
    }
  }
  closedir(dirp);
  if (!found) {
    (void)fprintf(stderr, "Failed to find hwmon device '%s'\n", value);
  }
}

char **tokenize_sensors(char *sensors_str, int *num_sensors) {
  int sensor_count = 1;
  char *delimiter = strchr(sensors_str, ':');

  while (1) {
    if (delimiter == NULL) {
      break;
    }
    sensor_count++;
    delimiter = strchr(delimiter + 1, ':');
  }

  *num_sensors = sensor_count;
  char **sensors = (char **)malloc(sensor_count * sizeof(*sensors));
  if (sensors == NULL) {
    perror("malloc");
    exit(EXIT_FAILURE);
  }

  char *sensors_str_copy = strdup(sensors_str);
  if (sensors_str_copy == NULL) {
    perror("strdup");
    exit(EXIT_FAILURE);
  }

  char *token = strtok(sensors_str_copy, ":");
  int index = 0;
  while (token != NULL) {
    sensors[index] = strdup(token);
    if (sensors[index] == NULL) {
      perror("strdup");
      exit(EXIT_FAILURE);
    }
    token = strtok(NULL, ":");
    index++;
  }
  free(sensors_str_copy);
  return sensors;
}

static bool read_label(const char *path, const char *d_name, char *label_buffer,
                       size_t buf_size) 
{
  char temp_label_path[PATH_MAX];

  int ret = snprintf(temp_label_path, sizeof(temp_label_path), "%s/%s", path, d_name);
  if (ret < 0) {
    perror("snprintf");
  }
  else if ((size_t)ret >= sizeof(temp_label_path)) {
    (void)fprintf(stderr, "Full path for %s exceeds PATH_MAX", d_name);
  }
  FILE *filep = fopen(temp_label_path, "r");
  if (filep == NULL) {
    (void)fprintf(stderr, "Failed to open %s: %s\n", temp_label_path, strerror(errno));
    exit(EXIT_FAILURE);
  }

  bool success = fgets(label_buffer, (int)buf_size, filep) != NULL;
  if (fclose(filep) == EOF) {
    perror("fclose");
  }
  return success;
}

static char *get_temp_input_path(const char *path, char **sensors,
                                 const char *label, int num_sensors,
                                 const char *d_name) 
{
  for (int i = 0; i < num_sensors; i++) {
    if (strncmp(label, sensors[i], strlen(sensors[i])) != 0) {
      continue;
    }
    char temp_input_number;
    if (sscanf(d_name, "temp%c_label", &temp_input_number) == EOF) {
      perror("sscanf");
      exit(EXIT_FAILURE);
    }
    char input_path_buffer[PATH_MAX];
    int ret = snprintf(input_path_buffer, sizeof(input_path_buffer), "%s/temp%c_input", path, temp_input_number);
    if (ret < 0) {
      perror("snprintf");
      exit(EXIT_FAILURE);
    }
    else if ((size_t)ret >= sizeof(input_path_buffer)) {
      (void)fprintf(stderr, "Full path for temp%c_input exceeds PATH_MAX", temp_input_number);
      exit(EXIT_FAILURE);
    }
    char *input_path = strdup(input_path_buffer);
    if (input_path == NULL) {
      perror("strdup");
      exit(EXIT_FAILURE);
    }
    return input_path;
  }
  return NULL;
}

void register_temp_inputs(const char *path, char **sensors, hwmonDevice *device) {
  DIR *dirp = opendir(path);
  if (dirp == NULL) {
    (void)fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
    exit(EXIT_FAILURE);
  }

  char *found_paths[LARGE_BUFFER];
  int found_count = 0;

  while (1) {
    errno = 0;
    struct dirent *direntp = readdir(dirp);

    if (errno > 0) {
      (void)fprintf(stderr, "Failed to read directory %s: %s\n", path, strerror(errno));
      exit(EXIT_FAILURE);
    }
    if (direntp == NULL) {
      break;
    }
    if (strncmp(direntp->d_name, "temp", 4) != 0 || strstr(direntp->d_name, "_label") == NULL) {
      continue;
    }

    char label_buffer[PAGE_SIZE];
    if (!read_label(path, direntp->d_name, label_buffer, sizeof(label_buffer))) {
      continue;
    }

    char *input_path = get_temp_input_path(path, sensors, label_buffer, device->num_sensors, direntp->d_name);
    if (input_path == NULL) {
      continue;
    }
    if (found_count < LARGE_BUFFER) {
      found_paths[found_count++] = input_path;
    }
    else {
      (void)fprintf(stderr, "Way too many sensors!\n");
      free(input_path);
      exit(EXIT_FAILURE);
    }
  }
  closedir(dirp);

  device->num_sensors = found_count;
  device->sensors = (char **)malloc(found_count * sizeof(device->sensors));
  if (device->sensors == NULL) {
    perror("malloc");
    exit(EXIT_FAILURE);
  }

  for (int i = 0; i < found_count; i++) {
    device->sensors[i] = found_paths[i];
  }
}

void load_config(struct config *cfg, char *path) {
  FILE *file = fopen(path, "r");
  char buffer[LARGE_BUFFER];
  int lineno = 1;

  if (file == NULL) {
    (void)fprintf(stderr, "Failed to open %s\n", path);
    exit(EXIT_FAILURE);
  }

  char **cpu_sensors;
  char cpu_path[PATH_MAX];

  char **gpu_sensors;
  char gpu_path[PATH_MAX];

  char pwm_path[PATH_MAX];
  char pwm_file[PATH_MAX];

  bool cpu_sensors_read = false;
  bool cpu_device_read = false;
  bool gpu_sensors_read = false;
  bool gpu_device_read = false;
  bool pwm_device_read = false;
  bool pwm_file_read = false;

  typedef enum { 
    PWM_DEVICE,
    PWM_FILE,
    CPU_DEVICE,
    CPU_SENSORS,
    GPU_DEVICE,
    GPU_SENSORS,
    GRAPH,
    MIN_PWM,
    MAX_PWM,
    AVERAGE,
    INTERVAL
  } ConfigKey;

  const struct {
    const char *name;
    ConfigKey key;
    bool *is_set;
  } config_key_map[] = {
    {"PWM_DEVICE", PWM_DEVICE, &pwm_device_read},
    {"PWM_FILE", PWM_FILE, &pwm_file_read},
    {"CPU_DEVICE", CPU_DEVICE, &cpu_device_read},
    {"CPU_SENSORS", CPU_SENSORS, &cpu_sensors_read},
    {"GPU_DEVICE", GPU_DEVICE, &gpu_device_read},
    {"GPU_SENSORS", GPU_SENSORS, &gpu_sensors_read},
    {"GRAPH", GRAPH, NULL},
    {"MIN_PWM", MIN_PWM, NULL},
    {"MAX_PWM", MAX_PWM, NULL},
    {"AVERAGE", AVERAGE, NULL},
    {"INTERVAL", INTERVAL, NULL}
  };

  int key_count = sizeof(config_key_map) / sizeof(config_key_map[0]);

  while (fgets(buffer, sizeof(buffer), file)) {
    if (buffer[0] == '#' || buffer[0] == '\n') {
      continue;
    }

    char *key = buffer;
    char *delimiter = strchr(buffer, '=');

    if (delimiter == NULL) {
      (void)fprintf(stderr, "Config: error on line %i\n", lineno);
      exit(EXIT_FAILURE);
    }
    
    delimiter[0] = '\0';
    char *value = delimiter + 1;

    char *newline = strchr(value, '\n');
    if (newline) {
      newline[0] = '\0';
    }

    for (int i = 0; i < key_count; i++) {
      if (strcmp(key, config_key_map[i].name) == 0) {
        switch (config_key_map[i].key) {
          case PWM_DEVICE:
            get_dev_path(value, pwm_path, sizeof(pwm_path));
            snprintf(cfg->pwm, sizeof(cfg->pwm), "%s/", pwm_path);
            *config_key_map[i].is_set = true;
            break;
          case PWM_FILE:
            snprintf(pwm_file, sizeof(pwm_file), "%s", value);
            *config_key_map[i].is_set = true;
            break;
          case CPU_DEVICE:
            get_dev_path(value, cpu_path, sizeof(cpu_path));
            *config_key_map[i].is_set = true;
            break;
          case CPU_SENSORS:
            cpu_sensors = tokenize_sensors(value, &cfg->cpu.num_sensors);
            *config_key_map[i].is_set = true;
            break;
          case GPU_DEVICE:
            get_dev_path(value, gpu_path, sizeof(gpu_path));
            *config_key_map[i].is_set = true;
            break;
          case GPU_SENSORS:
            gpu_sensors = tokenize_sensors(value, &cfg->gpu.num_sensors);
            *config_key_map[i].is_set = true;
            break;
          case GRAPH:
            snprintf(cfg->graph, sizeof(cfg->graph), "%s", value);
            break;
          case MIN_PWM:
            cfg->min_pwm = strtol(value, NULL, 0);
            break;
          case MAX_PWM:
            cfg->max_pwm = strtol(value, NULL, 0);
            break;
          case AVERAGE:
            cfg->average = strtol(value, NULL, 0);
            break;
          case INTERVAL:
            cfg->interval = strtol(value, NULL, 0) * 1000000;
            break;
        }
      }
    }

    if (debug && lineno == 1) {
      printf("\n%s:\n", path);
    }
    else if (debug) {
      printf("%s=%s\n", key, value);
    }
    lineno++;
  }

  for (int i = 0; i < key_count; i++) {
    if (config_key_map[i].is_set == false && config_key_map[i].is_set != NULL) {
      (void)fprintf(stderr, "Config: %s missing!\n", config_key_map[i].name);
      exit(EXIT_FAILURE);
    }
  }

  register_temp_inputs(cpu_path, cpu_sensors, &cfg->cpu);
  register_temp_inputs(gpu_path, gpu_sensors, &cfg->gpu);
  strncat(cfg->pwm, pwm_file, sizeof(cfg->pwm) - strlen(cfg->pwm) - 1);
  snprintf(cfg->pwm_enable, sizeof(cfg->pwm_enable), "%s%s", cfg->pwm, "_enable");
}

int (*load_graph(int *points, char *graph))[2] {
  FILE *file = fopen(graph, "r");
  char buffer[64];

  if (file == NULL) {
    fprintf(stderr, "Failed to open %s\n", graph);
    exit(EXIT_FAILURE);
  }

  *points = 1;
  while (fgets(buffer, sizeof(buffer), file)) {
    if (buffer[0] == '#' || buffer[0] == '\n') {
      continue;
    }
    (*points)++;
  }

  int (*curve)[2] = malloc(*points * sizeof(*curve));

  // Set initial graph coordinates to 0,0
  memset(curve, 0, sizeof(*curve));

  int count = 1;
  rewind(file);
  while (fgets(buffer, sizeof(buffer), file)) {
    if (buffer[0] == '#' || buffer[0] == '\n') {
      continue;
    }
    sscanf(buffer, "%i %i", &curve[count][0], &curve[count][1]);
    count++;
  }
  fclose(file);
  return curve;
}


