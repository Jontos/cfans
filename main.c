#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define ENABLE "/sys/class/hwmon/hwmon4/pwm3_enable"
#define PWM "/sys/class/hwmon/hwmon4/pwm3"
#define CPU_TEMP "/sys/class/hwmon/hwmon3/temp1_input"
#define GPU_TEMP "/sys/class/hwmon/hwmon5/temp1_input"
#define GRAPH "curve.dat"

#define MIN_PWM 40
#define MAX_PWM 255
#define AVERAGE 5

struct data {
  int cpu_temp;
  int gpu_temp;
  int curve[10][2];
};

void load_graph(int curve[][2]) {
  FILE *file = fopen(GRAPH, "r");
  char buffer[64];
  int count = 0;

  if (file == NULL) {
    fprintf(stderr, "fopen: failed to open %s\n", GRAPH);
    exit(EXIT_FAILURE);
  }

  while (fgets(buffer, sizeof(buffer), file)) {
    if (buffer[0] == '#' || buffer[0] == '\n') {
      continue;
    }
    sscanf(buffer, "%i %i", &curve[count][0], &curve[count][1]);
    count++;
  }
}

void enable() {

  FILE *file = fopen(ENABLE, "w");
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

void set_pwm(int val) {

  char val_buf[32];
  snprintf(val_buf, sizeof(val_buf), "%i", val);

  FILE *file = fopen(PWM, "w");
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
    perror("fopen");
    exit(EXIT_FAILURE);
  }

  if (fgets(buffer, sizeof(buffer), file) == NULL) {
    perror("fgets");
    exit(EXIT_FAILURE);
  }

  fclose(file);

  *temp = atoi(buffer) / 1000;
}

int main() {
  struct data data;

  int temp;
  int pwm_val;
  int prev_vals[AVERAGE];
  int buf_slot = AVERAGE;
  int avg_temp;
  int first_run = 1;

  enable();
  load_graph(data.curve);

  read_temp(CPU_TEMP, &data.cpu_temp);
  read_temp(GPU_TEMP, &data.gpu_temp);
  temp = (data.gpu_temp > data.cpu_temp) ? data.gpu_temp : data.cpu_temp;

  while (1) {

    // Reset averaging buffer index
    if (buf_slot % AVERAGE == 0) {
      buf_slot = 0;
    }

    read_temp(CPU_TEMP, &data.cpu_temp);
    read_temp(GPU_TEMP, &data.gpu_temp);
    temp = (data.gpu_temp > data.cpu_temp) ? data.gpu_temp : data.cpu_temp;

    // Populate averaging buffer with initial temp for first run
    if (first_run == 1) {
      for (int i = 0; i < AVERAGE; i++) {
        prev_vals[i] = temp;
      }
      first_run = 0;
    }
    else {
      prev_vals[buf_slot] = temp;
    }

    // Calculate average of previous values
    int total_vals = 0;
    for (int i = 0; i < AVERAGE; i++) {
      total_vals += prev_vals[i];
    }
    avg_temp = total_vals / AVERAGE;

    buf_slot++;

    for (int i = 0; i < (sizeof(data.curve) / sizeof(data.curve[0])); i++) {

      if (avg_temp < data.curve[i][0]) {

        int delta_from_node = avg_temp - data.curve[i-1][0];
        int percent_diff = data.curve[i][1] - data.curve[i-1][1];
        int temp_diff = data.curve[i][0] - data.curve[i-1][0];
        int fan_percent = delta_from_node * percent_diff / temp_diff + data.curve[i-1][1];

        int pwm_range = MAX_PWM - MIN_PWM;
        int pwm_val = fan_percent == 0 ? 0 : pwm_range / 100 * fan_percent + MIN_PWM;

        printf("avg_temp: %i  fan_percent: %i  pwm_val: %i\n", avg_temp, fan_percent, pwm_val);
        set_pwm(pwm_val);
        break;
      } 
    }
    sleep(1);
  }

}
