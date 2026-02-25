#include <cjson/cJSON.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

#define DEFAULT_INTERVAL 1000.0F // 1000ms

enum value_type {
  STRING,
  NUMBER,
  BOOL,
  ARRAY
};

struct config_option {
  const char *key;
  enum value_type type;
  void *struct_member;
  bool required;
};

struct config_layout {
  char *array_name;
  void **struct_array;
  size_t struct_size;
  int *object_count;

  const struct config_option *opts;
  int num_opts;

  int (*nested_conf_func)(void *userdata, cJSON *object, void *array_ptr);
  void *userdata;
};

struct child_array_layout {
  size_t array_offset;
  size_t count_offset;
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

int process_option(cJSON *item, struct config_option *opts)
{
  switch (opts->type) {
    case STRING:
      char *string = cJSON_GetStringValue(item);
      if (string == NULL) {
        (void)fprintf(stderr, "\"%s\" value must be of string type\n", opts->key);
        return -1;
      }

      *(char**)opts->struct_member = strdup(string);
      if (*(char**)opts->struct_member == NULL) {
        perror("strdup");
        return -1;
      }
      break;
    case NUMBER:
    case ARRAY:
      double number = cJSON_GetNumberValue(item);
      if (number == NAN) {
        (void)fprintf(stderr, "\"%s\" value must be a number\n", opts->key);
        return -1;
      }
      *(float*)opts->struct_member = (float)number;
      break;
    case BOOL:
      if (!cJSON_IsBool(item)) {
        (void)fprintf(stderr, "\"%s\" value must be a boolean\n", opts->key);
        return -1;
      }
      *(bool*)opts->struct_member = cJSON_IsTrue(item);
      break;
  }

  return 0;
}

int configure_opts(cJSON *json, struct config_option opts[], int num_opts)
{
  for (int i = 0; i < num_opts; i++) {

    cJSON *item = NULL;

    if (opts[i].type == ARRAY) {
      item = cJSON_GetArrayItem(json, i);
    }
    else {
      item = cJSON_GetObjectItem(json, opts[i].key);
    }

    if (item == NULL) {
      if (opts[i].required) {
        (void)fprintf(stderr, "Config error: missing %s\n", opts[i].key);
        return -1;
      }
      continue;
    }

    process_option(item, &opts[i]);
  }

  return 0;
}

int configure_config_object(cJSON *json, struct config_layout *sausage)
{
  cJSON *array = NULL;

  if (sausage->array_name != NULL) {
    array = cJSON_GetObjectItem(json, sausage->array_name);
    if (array == NULL) {
      (void)fprintf(stderr, "Config error: missing %s array\n", sausage->array_name);
      return -1;
    }
  }
  else {
    array = json;
  }

  *sausage->object_count = cJSON_GetArraySize(array);
  *sausage->struct_array = calloc(*sausage->object_count, sausage->struct_size);
  if (*sausage->struct_array == NULL) {
    perror("calloc");
    return -1;
  }

  cJSON *object = NULL;
  int count = 0;
  char *base_ptr = (char*)*sausage->struct_array;

  cJSON_ArrayForEach(object, array) {

    void *current_array_memb = base_ptr + count * sausage->struct_size;

    struct config_option opts[sausage->num_opts];

    for (int i = 0; i < sausage->num_opts; i++) {
      opts[i] = sausage->opts[i];

      size_t offset = (size_t)sausage->opts[i].struct_member;
      opts[i].struct_member = (char*)current_array_memb + offset;
    }

    if (configure_opts(object, opts, sausage->num_opts) < 0) return -1;

    if (sausage->nested_conf_func) {
      if (sausage->nested_conf_func(sausage->userdata, object, current_array_memb) > 0) return -1;
    }

    count++;
  }

  return 0;
}

int configure_general(cJSON *json, struct config *config)
{
  struct config_option opts[] = {
    {"interval", NUMBER, &config->interval, false}
  };

  config->interval = DEFAULT_INTERVAL;

  int num_opts = (sizeof(opts) / sizeof(opts[0]));

  return configure_opts(json, opts, num_opts);
}

int configure_sensors(void *layout_template, cJSON *json, void *parent_struct)
{
  struct child_array_layout *layout = layout_template;
  char *base_ptr = parent_struct;

  // NOLINTBEGIN(performance-no-int-to-ptr)
  static const struct config_option opts[] = {
    {"name", STRING, (void*)offsetof(struct sensor_config, name), true},
    {"offset", NUMBER, (void*)offsetof(struct sensor_config, offset), false}
  };
  // NOLINTEND(performance-no-int-to-ptr)

  return configure_config_object(json, &(struct config_layout) {
    .array_name = "sensors",
    .struct_array = (void**)(base_ptr + layout->array_offset),
    .struct_size = sizeof(struct sensor_config),
    .object_count = (int*)(base_ptr + layout->count_offset),
    .opts = opts,
    .num_opts = sizeof(opts) / sizeof(opts[0]),
  });
}

int configure_sources(cJSON *json, struct config *config)
{
  // NOLINTBEGIN(performance-no-int-to-ptr)
  static const struct config_option opts[] = {
    {"name", STRING, (void*)offsetof(struct source_config, name), true},
    {"device id", STRING, (void*)offsetof(struct source_config, device_id), false}
  };
  // NOLINTEND(performance-no-int-to-ptr)

  static struct child_array_layout layout = {
    .array_offset = offsetof(struct source_config, sensor),
    .count_offset = offsetof(struct source_config, num_sensors)
  };

  return configure_config_object(json, &(struct config_layout) {
    .array_name = "sources",
    .struct_array = (void**)&config->source,
    .struct_size = sizeof(struct source_config),
    .object_count = &config->num_sources,
    .opts = opts,
    .num_opts = sizeof(opts) / sizeof(opts[0]),
    .nested_conf_func = configure_sensors,
    .userdata = &layout
  });
}

int configure_graph(void *userdata, cJSON *json, void *curve_struct)
{
  (void)userdata;

  struct curve_config *curve = curve_struct;

  // NOLINTBEGIN(performance-no-int-to-ptr)
  static const struct config_option opts[] = {
    {"temp", ARRAY, (void*)offsetof(struct graph_point, temp), true},
    {"fan percent", ARRAY, (void*)offsetof(struct graph_point, fan_percent), true}
  };
  // NOLINTEND(performance-no-int-to-ptr)

  return configure_config_object(json, &(struct config_layout) {
    .array_name = "graph",
    .struct_array = (void**)&curve->graph_point,
    .struct_size = sizeof(struct graph_point),
    .object_count = &curve->num_points,
    .opts = opts,
    .num_opts = sizeof(opts) / sizeof(opts[0]),
  });
}

int configure_curves(cJSON *json, struct config *config)
{
  // NOLINTBEGIN(performance-no-int-to-ptr)
  static const struct config_option opts[] = {
    {"name", STRING, (void*)offsetof(struct curve_config, name), true},
    {"sensor", STRING, (void*)offsetof(struct curve_config, sensor), true},
    {"hysteresis", NUMBER, (void*)offsetof(struct curve_config, hysteresis), false},
    {"response time", NUMBER, (void*)offsetof(struct curve_config, response_time), false},
  };
  // NOLINTEND(performance-no-int-to-ptr)

  return configure_config_object(json, &(struct config_layout) {
    .array_name = "curves",
    .struct_array = (void**)&config->curve,
    .struct_size = sizeof(struct curve_config),
    .object_count = &config->num_curves,
    .opts = opts,
    .num_opts = sizeof(opts) / sizeof(opts[0]),
    .nested_conf_func = configure_graph
  });
}

int link_fan_curves(void *userdata, cJSON *json, void *fan_struct)
{
  struct fan_config *fan = fan_struct;
  struct config *config = userdata;

  cJSON *curve = cJSON_GetObjectItem(json, "curve");
  if (curve == NULL) {
    (void)fprintf(stderr, "Config error: \"%s\" has no curve selected\n", fan->name);
    return -1;
  }

  char *name = cJSON_GetStringValue(curve);
  if (name == NULL) {
    (void)fprintf(stderr, "\"curve\" value must be of string type\n");
    return -1;
  }
  
  for (int i = 0; i < config->num_curves; i++) {
    if (strcmp(name, config->curve[i].name) == 0) {
      fan->curve = &config->curve[i];
      return 0;
    }
  }

  (void)fprintf(stderr, "Config error: curve \"%s\" not found for fan \"%s\"", name, fan->name);

  return -1;
}

int configure_fans(cJSON *json, struct config *config)
{
  // NOLINTBEGIN(performance-no-int-to-ptr)
  static const struct config_option opts[] = {
    {"name", STRING, (void*)offsetof(struct fan_config, name), true},
    {"device id", STRING, (void*)offsetof(struct fan_config, device_id), true},
    {"pwm file", STRING, (void*)offsetof(struct fan_config, pwm_file), true},
    {"min pwm", NUMBER, (void*)offsetof(struct fan_config, min_pwm), true},
    {"max pwm", NUMBER, (void*)offsetof(struct fan_config, max_pwm), true},
    {"zero rpm", BOOL, (void*)offsetof(struct fan_config, zero_rpm), false},
  };
  // NOLINTEND(performance-no-int-to-ptr)

  return configure_config_object(json, &(struct config_layout) {
    .array_name = "fans",
    .struct_array = (void**)&config->fan,
    .struct_size = sizeof(struct fan_config),
    .object_count = &config->num_fans,
    .opts = opts,
    .num_opts = sizeof(opts) / sizeof(opts[0]),
    .nested_conf_func = link_fan_curves,
    .userdata = config
  });
}

int configure_custom_sensor_type(void *userdata, cJSON *json, void *custom_sensor_struct)
{
  (void)userdata;
  struct custom_sensor_config *struct_ptr = custom_sensor_struct;

  if (strcmp(struct_ptr->type, "max") == 0) {
    static struct child_array_layout layout = {
      .array_offset = offsetof(struct custom_sensor_config, type_opts.max.sensor),
      .count_offset = offsetof(struct custom_sensor_config, type_opts.max.num_sensors)
    };

    return configure_sensors(&layout, json, struct_ptr);
  }

  if (strcmp(struct_ptr->type, "file") == 0) {
    // NOLINTBEGIN(performance-no-int-to-ptr)
    struct config_option opts[] = {
      {"path", STRING, (char*)struct_ptr + offsetof(struct custom_sensor_config, type_opts.file.path), true},
    };
    // NOLINTEND(performance-no-int-to-ptr)

    return configure_opts(json, opts, sizeof(opts) / sizeof(opts[0]));
  }

  (void)fprintf(stderr, "Config error: unknown type \"%s\"\n", struct_ptr->type);
  return -1;
}

int configure_custom_sensors(cJSON *json, struct config *config)
{
  // NOLINTBEGIN(performance-no-int-to-ptr)
  static const struct config_option opts[] = {
    {"name", STRING, (void*)offsetof(struct custom_sensor_config, name), true},
    {"type", STRING, (void*)offsetof(struct custom_sensor_config, type), true}
  };
  // NOLINTEND(performance-no-int-to-ptr)

  return configure_config_object(json, &(struct config_layout) {
    .array_name = "custom sensors",
    .struct_array = (void**)&config->custom_sensor,
    .struct_size = sizeof(struct custom_sensor_config),
    .object_count = &config->num_custom_sensors,
    .opts = opts,
    .num_opts = sizeof(opts) / sizeof(opts[0]),
    .nested_conf_func = configure_custom_sensor_type,
  });
}

void free_config(struct config *config)
{
  for (int i = 0; i < config->num_sources; i++) {
    free(config->source[i].name);
    free(config->source[i].driver);
    free(config->source[i].device_id);

    for (int j = 0; j < config->source[i].num_sensors; j++) {
      free(config->source[i].sensor[j].name);
    }
    free(config->source[i].sensor);
  }
  free(config->source);

  for (int i = 0; i < config->num_fans; i++) {
    free(config->fan[i].name);
    free(config->fan[i].device_id);
    free(config->fan[i].pwm_file);
  }
  free(config->fan);

  for (int i = 0; i < config->num_curves; i++) {
    free(config->curve[i].name);
    free(config->curve[i].graph_point);
    free(config->curve[i].sensor);
  }
  free(config->curve);

  for (int i = 0; i < config->num_custom_sensors; i++) {
    free(config->custom_sensor[i].name);
    if (strcmp(config->custom_sensor[i].type, "max") == 0) {
      for (int j = 0; j < config->custom_sensor[i].type_opts.max.num_sensors; j++) {
        free(config->custom_sensor[i].type_opts.max.sensor[j].name);
      }
      free(config->custom_sensor[i].type_opts.max.sensor);
    }
    else if (strcmp(config->custom_sensor[i].type, "file") == 0) {
      free(config->custom_sensor[i].type_opts.file.path);
    }
    free(config->custom_sensor[i].type);
  }
  free(config->custom_sensor);
}

int load_config(const char *path, struct config *config)
{
  cJSON *json = parse_config(path);
  if (json == NULL) {
    (void)fprintf(stderr, "Error parsing config file\n");
    return -1;
  }

  int (*function[])(cJSON*, struct config*) = {
    configure_general,
    configure_sources,
    configure_curves,
    configure_custom_sensors,
    configure_fans,
  };

  int num_funcs = sizeof(function) / sizeof(function[0]);

  for (int i = 0; i < num_funcs; i++) {
    if (function[i](json, config)) {
      cJSON_Delete(json);
      return -1;
    }
  }

  cJSON_Delete(json);

  return 0;
}
