#include <dirent.h>
#include <errno.h>
#include <linux/limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

int debug = 0;

volatile sig_atomic_t keep_running = 1;

typedef struct {
  char **sensors;
  int num_sensors;
} hwmonDevice;

struct config {
  char pwm[PATH_MAX];
  char pwm_enable[PATH_MAX];

  hwmonDevice cpu;
  hwmonDevice gpu;

  char graph[PATH_MAX];

  int min_pwm;
  int max_pwm;
  int average;
  int interval;
};

void get_dev_path(char *key, char *value, char *dev_path, int size) {
  int found = 0;

  char *hwmon_path = "/sys/class/hwmon";
  DIR *dirp = opendir(hwmon_path);
  if (dirp == NULL) {
    fprintf(stderr, "Failed to open %s: %s\n", hwmon_path, strerror(errno));
    exit(EXIT_FAILURE);
  }

  while (1) {
    errno = 0;
    struct dirent *direntp = readdir(dirp);

    if (direntp == NULL) {
      if (errno > 0) {
        fprintf(stderr, "Failed to read directory %s: %s\n", hwmon_path, strerror(errno));
        exit(EXIT_FAILURE);
      }
      break;
    }

    if (direntp->d_name[0] == '.') {
      continue;
    }

    char namebuffer[64];
    char namefile[1024];
    snprintf(namefile, sizeof(namefile), "%s/%s/name", hwmon_path, direntp->d_name);

    FILE *filep = fopen(namefile, "r");
    if (filep == NULL) {
      fprintf(stderr, "Failed to open %s: %s\n", namefile, strerror(errno));
      exit(EXIT_FAILURE);
    }

    if (fgets(namebuffer, sizeof(namebuffer), filep) == NULL) {
      fprintf(stderr, "Failed to read %s: %s\n", namefile, strerror(errno));
      exit(EXIT_FAILURE);
    }
    fclose(filep);

    char *newline = strchr(namebuffer, '\n');
    if (newline) {
      newline[0] = '\0';
    }

    if (strcmp(namebuffer, value) == 0) {
      snprintf(dev_path, size, "%s/%s", hwmon_path, direntp->d_name);
      found = 1;
      break;
    }
  }
  closedir(dirp);
  if (!found) {
    fprintf(stderr, "Failed to find hwmon device '%s'\n", value);
  }
}

char **tokenize_sensors(char *sensors_str, int *num_sensors) {
  int sensor_count = 1;
  char *delimiter = strchr(sensors_str, ':');

  while (1) {
    if (delimiter == NULL) break;
    sensor_count++;
    delimiter = strchr(delimiter + 1, ':');
  }

  *num_sensors = sensor_count;
  char **sensors = malloc(sensor_count * sizeof(char *));
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
  int i = 0;
  while (token != NULL) {
    sensors[i] = strdup(token);
    if (sensors[i] == NULL) {
      perror("strdup");
      exit(EXIT_FAILURE);
    }
    token = strtok(NULL, ":");
    i++;
  }
  free(sensors_str_copy);
  return sensors;
}

void register_temp_inputs(char *path, char **sensors, hwmonDevice *device) {
  DIR *dirp = opendir(path);
  if (dirp == NULL) {
    fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
    exit(EXIT_FAILURE);
  }

  char *found_paths[1024];
  int found_count = 0;

  while (1) {
    errno = 0;
    struct dirent *direntp = readdir(dirp);

    if (direntp == NULL) {
      if (errno > 0) {
        fprintf(stderr, "Failed to read directory %s: %s\n", path, strerror(errno));
        exit(EXIT_FAILURE);
      }
      break;
    }

    if (direntp->d_name[0] == '.') {
      continue;
    }

    if (strncmp(direntp->d_name, "temp", 4) != 0 && strstr(direntp->d_name, "_label") != NULL) {
      continue;
    }

    char filename[PATH_MAX];
    snprintf(filename, sizeof(filename), "%s/%s", path, direntp->d_name);
    FILE *filep = fopen(filename, "r");
    if (filep == NULL) {
      fprintf(stderr, "Failed to open %s: %s\n", filename, strerror(errno));
      exit(EXIT_FAILURE);
    }

    char label_buffer[1024];
    fgets(label_buffer, sizeof(label_buffer), filep);
    fclose(filep);

    for (int i = 0; i < device->num_sensors; i++) {
      if (strncmp(label_buffer, sensors[i], strlen(sensors[i])) == 0) {
        char fileno;
        sscanf(direntp->d_name, "temp%c_label", &fileno);
        char input_path[64];
        snprintf(input_path, sizeof(input_path), "%s/temp%c_input", path, fileno);
        found_paths[found_count] = strdup(input_path);
        if (found_paths[found_count] == NULL) {
          perror("strdup");
          exit(EXIT_FAILURE);
        }
        found_count++;
        break;
      }
    }
  }
  closedir(dirp);

  device->num_sensors = found_count;
  device->sensors = malloc(found_count * sizeof(char *));
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
  char buffer[256];
  int lineno = 1;

  if (file == NULL) {
    fprintf(stderr, "Failed to open %s\n", path);
    exit(EXIT_FAILURE);
  }

  char **cpu_sensors;
  char cpu_path[PATH_MAX];

  char **gpu_sensors;
  char gpu_path[PATH_MAX];

  char pwm_path[PATH_MAX];
  char pwm_file[1024];

  bool cpu_sensors_read = false;
  bool cpu_device_read = false;

  bool gpu_sensors_read = false;
  bool gpu_device_read = false;

  bool pwm_device_read = false;
  bool pwm_file_read = false;

  while (fgets(buffer, sizeof(buffer), file)) {
    if (buffer[0] == '#' || buffer[0] == '\n') {
      continue;
    }

    char *key = buffer;
    char *delimiter = strchr(buffer, '=');

    if (delimiter == NULL) {
      fprintf(stderr, "Config: error on line %i\n", lineno);
      exit(EXIT_FAILURE);
    }
    
    delimiter[0] = '\0';
    char *value = delimiter + 1;

    char *newline = strchr(value, '\n');
    if (newline) {
      newline[0] = '\0';
    }

    if (strcmp(key, "PWM_DEVICE") == 0) {
      get_dev_path(key, value, pwm_path, sizeof(pwm_path));
      snprintf(cfg->pwm, sizeof(cfg->pwm), "%s/", pwm_path);
      pwm_device_read = true;
    }
    else if (strcmp(key, "PWM_FILE") == 0) {
      snprintf(pwm_file, sizeof(pwm_file), "%s", value);
      pwm_file_read = true;
    }
    else if (strcmp(key, "CPU_DEVICE") == 0) {
      get_dev_path(key, value, cpu_path, sizeof(cpu_path));
      cpu_device_read = true;
    }
    else if (strcmp(key, "CPU_SENSORS") == 0) {
      cpu_sensors = tokenize_sensors(value, &cfg->cpu.num_sensors);
      cpu_sensors_read = true;
    }
    else if (strcmp(key, "GPU_DEVICE") == 0) {
      get_dev_path(key, value, gpu_path, sizeof(gpu_path));
      gpu_device_read = true;
    }
    else if (strcmp(key, "GPU_SENSORS") == 0) {
      gpu_sensors = tokenize_sensors(value, &cfg->gpu.num_sensors);
      gpu_sensors_read = true;
    }
    else if (strcmp(key, "GRAPH") == 0) {
      snprintf(cfg->graph, sizeof(cfg->graph), "%s", value);
    }
    else if (strcmp(key, "MIN_PWM") == 0) {
      cfg->min_pwm = strtol(value, NULL, 0);
    }
    else if (strcmp(key, "MAX_PWM") == 0) {
      cfg->max_pwm = strtol(value, NULL, 0);
    }
    else if (strcmp(key, "AVERAGE") == 0) {
      cfg->average = strtol(value, NULL, 0);
    }
    else if (strcmp(key, "INTERVAL") == 0) {
      cfg->interval = strtol(value, NULL, 0) * 1000000;
    }

    if (debug) {
      if (lineno == 1) {
        printf("\n%s:\n", path);
      }
      printf("%s=%s\n", key, value);
    }

    lineno++;
  }

  if (cpu_device_read == false) {
    fprintf(stderr, "Config: CPU_DEVICE missing!\n");
    exit(EXIT_FAILURE);
  }
  if (gpu_device_read == false) {
    fprintf(stderr, "Config: GPU_DEVICE missing!\n");
    exit(EXIT_FAILURE);
  }
  if (pwm_device_read == false) {
    fprintf(stderr, "Config: PWM_DEVICE missing!\n");
    exit(EXIT_FAILURE);
  }
  if (cpu_sensors_read == false) {
    fprintf(stderr, "Config: CPU_SENSORS missing!\n");
    exit(EXIT_FAILURE);
  }
  if (gpu_sensors_read == false) {
    fprintf(stderr, "Config: GPU_SENSORS missing!\n");
    exit(EXIT_FAILURE);
  }
  if (pwm_file_read == false) {
    fprintf(stderr, "Config: PWM_FILE missing!\n");
    exit(EXIT_FAILURE);
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

void enable_pwm(struct config *cfg, char *og_value) {
  FILE *file = fopen(cfg->pwm_enable, "r+");
  if (!file) {
    fprintf(stderr, "Failed to open %s: %s\n", cfg->pwm_enable, strerror(errno));
    exit(EXIT_FAILURE);
  }

  if (fgets(og_value, sizeof(og_value), file) == NULL) {
    fprintf(stderr, "Failed to read %s: %s\n", cfg->pwm_enable, strerror(errno));
  }

  if (fputc('1', file) == EOF) {
    fprintf(stderr, "Failed to write to %s: %s\n", cfg->pwm_enable, strerror(errno));
    exit(EXIT_FAILURE);
  }
  fclose(file);
}

void set_pwm(int val, char *pwm) {
  char val_buf[32];
  snprintf(val_buf, sizeof(val_buf), "%i", val);

  FILE *file = fopen(pwm, "w");
  if (!file) {
    fprintf(stderr, "Failed to open %s: %s\n", pwm, strerror(errno));
    exit(EXIT_FAILURE);
  }

  if (fputs(val_buf, file) == EOF) {
    fprintf(stderr, "Failed to write to %s: %s\n", pwm, strerror(errno));
    exit(EXIT_FAILURE);
  }
  fclose(file);
}

void read_temp(hwmonDevice device, int *temp) {
  FILE *file;
  char buffer[32];
  int highest_temp = 0;

  for (int i = 0; i < device.num_sensors; i++) {
    file = fopen(device.sensors[i], "r");
    if (!file) {
      fprintf(stderr, "fopen: failed to open %s\n", device.sensors[i]);
      exit(EXIT_FAILURE);
    }

    if (fgets(buffer, sizeof(buffer), file) == NULL) {
      fprintf(stderr, "fgets: failed to read %s\n", device.sensors[i]);
      exit(EXIT_FAILURE);
    }
    fclose(file);

    int temp_buffer = atoi(buffer) / 1000;
    highest_temp = (temp_buffer > highest_temp) ? temp_buffer : highest_temp;
  }
  *temp = highest_temp;
}

void signal_handler(int signum) {
  keep_running = 0;
}

int main(int argc, char *argv[]) {
  int opt;
  char *config_path = "/etc/cfans/fancontrol.conf";
  struct config cfg;

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  while ((opt = getopt(argc, argv, "c:d")) != -1) {
    switch (opt) {
      case 'c':
        config_path = optarg;
        break;
      case 'd':
        debug = 1;
        break;
      case '?':
        fprintf(stderr, "Usage: %s [-c CONFIG_FILE] [-d]\n", argv[0]);
        exit(EXIT_FAILURE);
    } 
  }

  load_config(&cfg, config_path);

  int cpu_temp;
  int gpu_temp;

  int prev_vals[cfg.average];
  int buf_slot = cfg.average;
  int first_run = 1;
  int last_val;

  int points;
  int (*graph_curve)[2];
  graph_curve = load_graph(&points, cfg.graph);

  char og_pwm_enable_value[4];
  enable_pwm(&cfg, og_pwm_enable_value);

  if (debug) {
    printf("\n%s:\n", cfg.graph);
    for (int i = 0; i < points; i++) {
      for (int j = 0; j < 2; j++) {
        printf("%i ", graph_curve[i][j]);
      }
      printf("\n");
    }
    printf("Total points: %i\n\n", points);
  }

  while (keep_running) {
    // Reset averaging buffer index
    if (buf_slot % cfg.average == 0) {
      buf_slot = 0;
    }

    read_temp(cfg.cpu, &cpu_temp);
    read_temp(cfg.gpu, &gpu_temp);
    int highest_temp = (gpu_temp > cpu_temp) ? gpu_temp : cpu_temp;

    // Populate averaging buffer with initial temp for first run
    if (first_run == 1) {
      for (int i = 0; i < cfg.average; i++) {
        prev_vals[i] = highest_temp;
      }
    }
    else {
      prev_vals[buf_slot] = highest_temp;
    }

    // Calculate average of previous values
    int total_vals = 0;
    for (int i = 0; i < cfg.average; i++) {
      total_vals += prev_vals[i];
    }
    int avg_temp = total_vals / cfg.average;

    buf_slot++;

    for (int i = 0; i < points; i++) {
      if (avg_temp < graph_curve[i][0]) {
        int delta_from_node = avg_temp - graph_curve[i-1][0];
        int percent_diff = graph_curve[i][1] - graph_curve[i-1][1];
        int temp_diff = graph_curve[i][0] - graph_curve[i-1][0];
        int fan_percent = delta_from_node * percent_diff / temp_diff + graph_curve[i-1][1];

        int pwm_range = cfg.max_pwm - cfg.min_pwm;
        int pwm_val = fan_percent == 0 ? 0 : pwm_range / 100 * fan_percent + cfg.min_pwm;

        if (first_run == 1) {
          last_val = pwm_val;
          first_run = 0;
        }
        else if (pwm_val > last_val) {
          last_val++;
        }
        else if (pwm_val < last_val) {
          last_val--;
        }

        if (debug) {
          printf("\ravg: %i fan: %i pwm: %i last: %i",
                  avg_temp, fan_percent, pwm_val, last_val);
          fflush(stdout);
        }

        set_pwm(last_val, cfg.pwm);
        break;
      } 
    }
    struct timespec interval;
    interval.tv_nsec = cfg.interval;
    nanosleep(&interval, NULL);
  }
  // Run cleanup..
  free(graph_curve);
  free(cfg.cpu.sensors);
  free(cfg.gpu.sensors);

  printf("\nResetting automatic fan control..\n");

  FILE *file = fopen(cfg.pwm_enable, "w");
  if (!file) {
    fprintf(stderr, "Failed to open %s: %s\n", cfg.pwm_enable, strerror(errno));
    exit(EXIT_FAILURE);
  }

  if (fputs(og_pwm_enable_value, file) == EOF) {
    fprintf(stderr, "Failed to write to %s: %s\n", cfg.pwm_enable, strerror(errno));
    exit(EXIT_FAILURE);
  }
  fclose(file);

}
