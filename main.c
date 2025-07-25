#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct config {
  char pwm[256];
  char cpu_temp[256];
  char gpu_temp[256];
  char graph[256];

  int min_pwm;
  int max_pwm;
  int average;
};

void load_config(struct config *cfg, char *path) {
  FILE *file = fopen(path, "r");
  char buffer[256];
  int lineno = 1;

  if (file == NULL) {
    fprintf(stderr, "fopen: failed to open %s\n", path);
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
    
    *delimiter = '\0';
    char *value = delimiter + 1;

    char *newline = strchr(value, '\n');
    if (newline) {
      newline[0] = '\0';
    }

    if (strcmp(key, "PWM") == 0) {
      snprintf(cfg->pwm, sizeof(cfg->pwm), "/sys/class/hwmon/%s", value);
    }
    else if (strcmp(key, "CPU_TEMP") == 0) {
      snprintf(cfg->cpu_temp, sizeof(cfg->cpu_temp), "/sys/class/hwmon/%s", value);
    }
    else if (strcmp(key, "GPU_TEMP") == 0) {
      snprintf(cfg->gpu_temp, sizeof(cfg->gpu_temp), "/sys/class/hwmon/%s", value);
    }
    else if (strcmp(key, "GRAPH") == 0) {
      snprintf(cfg->graph, sizeof(cfg->graph), "%s", value);
    }
    else if (strcmp(key, "MIN_PWM") == 0) {
      cfg->min_pwm = atoi(value);
    }
    else if (strcmp(key, "MAX_PWM") == 0) {
      cfg->max_pwm = atoi(value);
    }
    else if (strcmp(key, "AVERAGE") == 0) {
      cfg->average = atoi(value);
    }

    lineno++;
  }
}

void load_graph(int curve[][2], char *graph) {
  FILE *file = fopen(graph, "r");
  char buffer[64];
  int count = 0;

  if (file == NULL) {
    fprintf(stderr, "fopen: failed to open %s\n", graph);
    exit(EXIT_FAILURE);
  }

  while (fgets(buffer, sizeof(buffer), file)) {
    if (buffer[0] == '#' || buffer[0] == '\n') {
      continue;
    }
    sscanf(buffer, "%i %i", &curve[count][0], &curve[count][1]);
    count++;
  }
  fclose(file);
}

void enable_pwm(char *pwm) {
  char pwm_enable[256];
  snprintf(pwm_enable, sizeof(pwm_enable), "%s_enable", pwm);

  FILE *file = fopen(pwm_enable, "w");
  if (!file) {
    perror("fopen");
    exit(EXIT_FAILURE);
  }

  int ret = fputc('1', file);
  if (ret == EOF) {
    fprintf(stderr, "fputc() failed: %i\n", ret);
    exit(EXIT_FAILURE);
  }

  fclose(file);
}

void set_pwm(int val, char *pwm) {

  char val_buf[32];
  snprintf(val_buf, sizeof(val_buf), "%i", val);

  FILE *file = fopen(pwm, "w");
  if (!file) {
    perror("fopen");
    exit(EXIT_FAILURE);
  }

  int ret = fputs(val_buf, file);
  if (ret == EOF) {
    fprintf(stderr, "fputc() failed: %i\n", ret);
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

int main() {
  struct config cfg;

  load_config(&cfg, "/etc/cfans/fancontrol.conf");

  int cpu_temp;
  int gpu_temp;
  int curve[10][2];

  int prev_vals[cfg.average];
  int buf_slot = cfg.average;
  int first_run = 1;
  int last_val;

  enable_pwm(cfg.pwm);
  load_graph(curve, cfg.graph);

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

    for (int i = 0; i < (sizeof(curve) / sizeof(curve[0])); i++) {

      if (avg_temp < curve[i][0]) {

        int delta_from_node = avg_temp - curve[i-1][0];
        int percent_diff = curve[i][1] - curve[i-1][1];
        int temp_diff = curve[i][0] - curve[i-1][0];
        int fan_percent = delta_from_node * percent_diff / temp_diff + curve[i-1][1];

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

        // fprintf(stderr, "avg: %i fan: %i pwm: %i last: %i\n",
        //         avg_temp, fan_percent, pwm_val, last_val);

        set_pwm(last_val, cfg.pwm);
        break;
      } 
    }
    sleep(1);
  }
}
