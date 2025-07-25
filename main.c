#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int debug = 0;

struct config {
  char pwm[256];
  char cpu_temp[256];
  char gpu_temp[256];
  char graph[256];

  int min_pwm;
  int max_pwm;
  int average;
  int interval;
};

struct cleanup_data {
  char pwm_enable[256];
  char pwm_enable_og_val[8];
  int (*graph_curve)[2];
};

void get_dev_path(char *key, char *value, char *conf_opt, int size) {
  char *device = value;
  char *delimiter = strchr(value, '/');
  int found = 0;

  if (delimiter == NULL) {
    fprintf(stderr, "Config: missing filename for %s\n", key);
    exit(EXIT_FAILURE);
  }

  delimiter[0] = '\0';
  char *filename = delimiter + 1;

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
        fprintf(stderr, "Failed to read contents of %s: %s\n", hwmon_path, strerror(errno));
        exit(EXIT_FAILURE);
      }
      break;
    }

    if (direntp->d_name[0] == '.') {
      continue;
    }

    char buffer[64];
    char namefile[64];
    snprintf(namefile, sizeof(namefile), "%s/%s/name", hwmon_path, direntp->d_name);

    FILE *filep = fopen(namefile, "r");
    if (filep == NULL) {
      fprintf(stderr, "Failed to open %s: %s\n", namefile, strerror(errno));
      exit(EXIT_FAILURE);
    }

    if (fgets(buffer, sizeof(buffer), filep) == NULL) {
      fprintf(stderr, "Failed to read %s: %s\n", namefile, strerror(errno));
      exit(EXIT_FAILURE);
    }
    fclose(filep);

    char *newline = strchr(buffer, '\n');
    if (newline) {
      newline[0] = '\0';
    }

    if (strcmp(buffer, device) == 0) {
      snprintf(conf_opt, size, "%s/%s/%s", hwmon_path, direntp->d_name, filename);
      found = 1;
      break;
    }
  }
  closedir(dirp);
  if (!found) {
    fprintf(stderr, "Failed to find hwmon device '%s'\n", device);
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

    if (strcmp(key, "PWM") == 0) {
      get_dev_path(key, value, cfg->pwm, sizeof(cfg->pwm));
    }
    else if (strcmp(key, "CPU_TEMP") == 0) {
      get_dev_path(key, value, cfg->cpu_temp, sizeof(cfg->cpu_temp));
    }
    else if (strcmp(key, "GPU_TEMP") == 0) {
      get_dev_path(key, value, cfg->gpu_temp, sizeof(cfg->gpu_temp));
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
      cfg->interval = strtol(value, NULL, 0);
    }

    if (debug) {
      if (lineno == 1) {
        printf("\n%s:\n", path);
      }
      printf("%s=%s\n", key, value);
    }

    lineno++;
  }
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
    *points += 1;
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

void enable_pwm(char *pwm, void *cleanup) {
  struct cleanup_data *data = cleanup;

  snprintf(data->pwm_enable, sizeof(data->pwm_enable), "%s_enable", pwm);
  int original_value;

  FILE *file = fopen(data->pwm_enable, "r+");
  if (!file) {
    fprintf(stderr, "Failed to open %s: %s\n", data->pwm_enable, strerror(errno));
    exit(EXIT_FAILURE);
  }

  if (fgets(data->pwm_enable_og_val, sizeof(data->pwm_enable_og_val), file) == NULL) {
    fprintf(stderr, "Failed to read %s: %s\n", data->pwm_enable, strerror(errno));
  }

  if (fputc('1', file) == EOF) {
    fprintf(stderr, "Failed to write to %s: %s\n", data->pwm_enable, strerror(errno));
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

void read_temp(char *path, int *temp) {
  FILE *file;
  char buffer[32];

  file = fopen(path, "r");
  if (!file) {
    fprintf(stderr, "fopen: failed to open %s\n", path);
    exit(EXIT_FAILURE);
  }

  if (fgets(buffer, sizeof(buffer), file) == NULL) {
    fprintf(stderr, "fgets: failed to read %s\n", path);
    exit(EXIT_FAILURE);
  }
  fclose(file);

  *temp = atoi(buffer) / 1000;
}

void cleanup(int status, void *arg) {
  struct cleanup_data *data = arg;

  free(data->graph_curve);

  printf("\nResetting automatic fan control..\n");

  FILE *file = fopen(data->pwm_enable, "w");
  if (!file) {
    perror("Failed to reset automatic fan control");
    exit(EXIT_FAILURE);
  }

  if (fputs(data->pwm_enable_og_val, file) == EOF) {
    perror("Failed to reset automatic fan control");
    exit(EXIT_FAILURE);
  }
  fclose(file);
}

void signal_handler(int signum) {
  exit(signum);
}

int main(int argc, char *argv[]) {
  int opt;
  char *config_path = "/etc/cfans/fancontrol.conf";
  struct config cfg;
  struct cleanup_data data;

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  on_exit(cleanup, &data);

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

  data.graph_curve = load_graph(&points, cfg.graph);
  enable_pwm(cfg.pwm, &data);

  if (debug) {
    printf("\n%s:\n", cfg.graph);
    for (int i = 0; i < points; i++) {
      for (int j = 0; j < 2; j++) {
        printf("%i ", data.graph_curve[i][j]);
      }
      printf("\n");
    }
    printf("Total points: %i\n\n", points);
  }

  while (1) {
    // Reset averaging buffer index
    if (buf_slot % cfg.average == 0) {
      buf_slot = 0;
    }

    read_temp(cfg.cpu_temp, &cpu_temp);
    read_temp(cfg.gpu_temp, &gpu_temp);
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
      if (avg_temp < data.graph_curve[i][0]) {
        int delta_from_node = avg_temp - data.graph_curve[i-1][0];
        int percent_diff = data.graph_curve[i][1] - data.graph_curve[i-1][1];
        int temp_diff = data.graph_curve[i][0] - data.graph_curve[i-1][0];
        int fan_percent = delta_from_node * percent_diff / temp_diff + data.graph_curve[i-1][1];

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
          printf("avg: %i fan: %i pwm: %i last: %i\n",
                  avg_temp, fan_percent, pwm_val, last_val);
        }

        set_pwm(last_val, cfg.pwm);
        break;
      } 
    }
    sleep(cfg.interval);
  }
}
