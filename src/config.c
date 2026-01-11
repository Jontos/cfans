#include <cjson/cJSON.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

enum value_type {
  STRING,
  NUMBER,
  BOOL
};

struct config_option {
  const char *key;
  enum value_type type;
  cJSON *json;
  void *struct_member;
  bool required;
};

cJSON *parse_config(const char *path)
{
  cJSON *ret = NULL;

  FILE *file = fopen(path, "r");

  if (file == NULL) {
    (void)fprintf(stderr, "Failed to open %s\n", path);
    return ret;
  }

  char* buffer = NULL;
  size_t len;
  if (getdelim(&buffer, &len, '\0', file) != -1) {
    ret = cJSON_Parse(buffer);
  }
  else if (ferror(file)) {
    perror("getdelim");
  }

  if (fclose(file) == EOF) {
    perror("fclose");
  }

  free(buffer);
  return ret;
}

int configure_opts(cJSON *json, struct config_option opts[], int num_opts)
{
  for (int i = 0; i < num_opts; i++) {

    if (json != NULL) {
      opts[i].json = cJSON_GetObjectItem(json, opts[i].key);
      if (opts[i].json == NULL) {
        if (opts[i].required) {
          (void)fprintf(stderr, "Config error: missing %s\n", opts[i].key);
          return -1;
        }
        continue;
      }
    }

    switch (opts[i].type) {
      case STRING:
        char *string = cJSON_GetStringValue(opts[i].json);
        if (string == NULL) {
          (void)fprintf(stderr, "\"%s\" value must be of string type\n", opts[i].key);
          return -1;
        }
        *(char**)opts[i].struct_member = strdup(string);
        if (opts[i].struct_member == NULL) {
          perror("strdup");
          return -1;
        }
        break;
      case NUMBER:
        double number = cJSON_GetNumberValue(opts[i].json);
        if (number == NAN) {
          (void)fprintf(stderr, "\"%s\" value must be a number\n", opts[i].key);
          return -1;
        }
        *(int*)opts[i].struct_member = (int)number;
        break;
      case BOOL:
        if (!cJSON_IsBool(opts[i].json)) {
          (void)fprintf(stderr, "\"%s\" value must be a boolean\n", opts[i].key);
          return -1;
        }
        *(bool*)opts[i].struct_member = cJSON_IsTrue(opts[i].json);
        break;
    }
  }

  return 0;
}

int configure_general(cJSON *json, struct config *config)
{
  cJSON *average = NULL;
  cJSON *interval = NULL;

  struct config_option opts[] = {
    {"average", NUMBER, average, &config->average, true},
    {"interval", NUMBER, interval, &config->interval, true}
  };

  int num_opts = (sizeof(opts) / sizeof(struct config_option));

  return configure_opts(json, opts, num_opts);
}

int configure_sensors(cJSON *array, struct source *source)
{
  source->num_sensors = cJSON_GetArraySize(array);
  source->sensor = calloc(source->num_sensors, sizeof(struct sensor));
  if (source->sensor == NULL) {
    perror("calloc");
    return -1;
  }

  cJSON *sensor = NULL;
  int count = 0;
  cJSON_ArrayForEach(sensor, array) {
    cJSON *name = NULL;
    cJSON *offset = NULL;

    struct config_option opts[] = {
      {"name", STRING, name, (void*)&source->sensor[count].name, true},
      {"offset", NUMBER, offset, &source->sensor[count].offset, false}
    };

    int num_opts = (sizeof(opts) / sizeof(struct config_option));

    if (configure_opts(sensor, opts, num_opts) < 0) return -1;
    
    count++;
  }

  return 0;
}

int configure_sources(cJSON *array, struct config *config)
{
  config->num_sources = cJSON_GetArraySize(array);
  config->source = calloc(config->num_sources, sizeof(struct source));
  if (config->source == NULL) {
    perror("calloc");
    return -1;
  }

  cJSON *source = NULL;
  int count = 0;
  cJSON_ArrayForEach(source, array) {

    cJSON *name = NULL;
    cJSON *driver = NULL;
    cJSON *pci_device = NULL;

    cJSON *sensors = cJSON_GetObjectItem(source, "sensors");
    if (sensors == NULL) {
      (void)fprintf(stderr, "Config error: missing sensor array\n");
      return -1;
    }

    if (configure_sensors(sensors, &config->source[count]) < 0) return -1;

    struct config_option opts[] = {
      {"name", STRING, name, (void*)&config->source[count].name, true},
      {"driver", STRING, driver, (void*)&config->source[count].driver, true},
      {"pci device", STRING, pci_device, (void*)&config->source[count].pci_device, false}
    };

    int num_opts = (sizeof(opts) / sizeof(struct config_option));

    if (configure_opts(source, opts, num_opts) < 0) return -1;

    count++;
  }

  return 0;
}

int configure_curve(cJSON *array, struct fan *fan)
{
  fan->num_points = cJSON_GetArraySize(array);
  fan->curve = calloc(fan->num_points, sizeof(struct curve));
  if (fan->curve == NULL) {
    perror("calloc");
    return -1;
  }

  cJSON *point = NULL;
  int count = 0;
  cJSON_ArrayForEach(point, array) {
    cJSON *temp = point->child;
    cJSON *fan_percent = point->child->next;

    struct config_option opts[] = {
      {"temp", NUMBER, temp, &fan->curve[count].temp, true},
      {"fan percent", NUMBER, fan_percent, &fan->curve[count].fan_percent, true}
    };

    int num_opts = (sizeof(opts) / sizeof(struct config_option));

    if (configure_opts(NULL, opts, num_opts) < 0) return -1;

    count++;
  }
  return 0;
}

int configure_fans(cJSON *array, struct config *config)
{
  config->num_fans = cJSON_GetArraySize(array);
  config->fan = calloc(config->num_fans, sizeof(struct fan));
  if (config->fan == NULL) {
    perror("calloc");
    return -1;
  }

  cJSON *fan = NULL;
  int count = 0;
  cJSON_ArrayForEach(fan, array) {

    cJSON *name = NULL;
    cJSON *driver = NULL;
    cJSON *pwm_file = NULL;
    cJSON *min_pwm = NULL;
    cJSON *max_pwm = NULL;
    cJSON *zero_rpm = NULL;

    cJSON *curve = cJSON_GetObjectItem(fan, "curve");
    if (curve == NULL) {
      (void)fprintf(stderr, "Config error: missing fan curve\n");
      return -1;
    }

    if (configure_curve(curve, &config->fan[count]) < 0) return -1;

    struct config_option opts[] = {
      {"name", STRING, name, (void*)&config->fan[count].name, true},
      {"driver", STRING, driver, (void*)&config->fan[count].driver, true},
      {"pwm file", STRING, pwm_file, (void*)&config->fan[count].pwm_file, true},
      {"min pwm", NUMBER, min_pwm, &config->fan[count].min_pwm, true},
      {"max pwm", NUMBER, max_pwm, &config->fan[count].max_pwm, true},
      {"zero rpm", BOOL, zero_rpm, &config->fan[count].zero_rpm, false},
    };

    int num_opts = (sizeof(opts) / sizeof(struct config_option));

    if (configure_opts(fan, opts, num_opts) < 0) return -1;

    count++;
  }

  return 0;
}

void free_config(struct config *config)
{
  for (int i = 0; i < config->num_sources; i++) {
    free(config->source[i].name);
    free(config->source[i].driver);
    free(config->source[i].pci_device);

    for (int j = 0; j < config->source[i].num_sensors; j++) {
      free(config->source[i].sensor[j].name);
    }
    free(config->source[i].sensor);
  }
  free(config->source);

  for (int i = 0; i < config->num_fans; i++) {
    free(config->fan[i].name);
    free(config->fan[i].driver);
    free(config->fan[i].pwm_file);
    free(config->fan[i].curve);
  }
  free(config->fan);
}

int load_config(const char *path, struct config *config)
{
  cJSON *json = parse_config(path);
  if (json == NULL) {
    (void)fprintf(stderr, "Error parsing config file\n");
    return -1;
  }

  if (configure_general(json, config) < 0) {
    cJSON_Delete(json);
    return -1;
  }

  cJSON *sources = cJSON_GetObjectItem(json, "sources");
  if (sources == NULL) {
    (void)fprintf(stderr, "Config error: missing sources array\n");
    cJSON_Delete(json);
    return -1;
  }
  if (configure_sources(sources, config) < 0) {
    cJSON_Delete(json);
    return -1;
  }

  cJSON *fans = cJSON_GetObjectItem(json, "fans");
  if (fans == NULL) {
    (void)fprintf(stderr, "Config error: missing fans array\n");
    cJSON_Delete(json);
    return -1;
  }
  if (configure_fans(fans, config) < 0) {
    cJSON_Delete(json);
    return -1;
  }

  cJSON_Delete(json);

  return 0;
}
