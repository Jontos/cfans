#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config_parser.h"
#include "utils.h"
#include "ini.h"

typedef enum {
    DUPSTR,
    STRTOL,
    STRTOF
} ActionType;

typedef struct {
    const char *key;
    ActionType action;
    size_t offset;
} ConfigHandler;

static const ConfigHandler general_handlers[] = {
  {"Graph", DUPSTR, offsetof(Config, graph_file)},
  {"Average", STRTOL, offsetof(Config, average)},
  {"Interval", STRTOL, offsetof(Config, interval)},
  {NULL, 0, 0}
};

static const ConfigHandler source_handlers[] = {
  {"Name", DUPSTR, offsetof(Source, name)},
  {"Driver", DUPSTR, offsetof(Source, driver)},
  {"PciDevice", DUPSTR, offsetof(Source, pci_device)},
  {"Sensors", DUPSTR, offsetof(Source, sensors_string)},
  {"Scale", STRTOF, offsetof(Source, scale)},
  {NULL, 0, 0}
};

static const ConfigHandler fan_handlers[] = {
  {"Name", DUPSTR, offsetof(Fan, name)},
  {"Driver", DUPSTR, offsetof(Fan, driver)},
  {"PWMFile", DUPSTR, offsetof(Fan, pwm_file)},
  {"MinPWM", STRTOL, offsetof(Fan, min_pwm)},
  {"MaxPWM", STRTOL, offsetof(Fan, max_pwm)},
  {"TempSources", DUPSTR, offsetof(Fan, temp_sources_string)},
  {NULL, 0, 0}
};

void *create_object(void **object, int *count, int *capacity, size_t obj_size) {
  if (*count == *capacity) {
    if (resize_array(object, obj_size, capacity) < 0) {
      return NULL;
    }
  }

  void *new_object = (char*)(*object) + (*count * obj_size);

  memset(new_object, 0, obj_size);
  (*count)++;
  return new_object;
}

int action_taker(void *member_ptr, ActionType action, const char *value) {
  char *endptr;
  errno = 0;
  switch (action) {
    case DUPSTR:
      *(char**)member_ptr = strdup(value);
      if (*(char**)member_ptr == NULL) {
        perror("strdup failed");
        return -1;
      }
      return 0;
    case STRTOL:
      *(int*)member_ptr = (int)strtol(value, &endptr, 0);
      if (value == endptr) {
        (void)fprintf(stderr, "No digits found in value: %s\n", value);
        return -1;
      }
      if (errno != 0) {
        perror("strtol");
      }
      return 0;
    case STRTOF:
      *(float*)member_ptr = strtof(value, &endptr);
      if (value == endptr) {
        (void)fprintf(stderr, "No digits found in value: %s\n", value);
        return -1;
      }
      if (errno != 0) {
        perror("strtof");
      }
      return 0;
  }
  return -1;
}

int configure_general(Config *config, const char *key, const char *value) {
  for (int i = 0; general_handlers[i].key != NULL; i++) {
    if (strcmp(key, general_handlers[i].key) == 0) {
      const ConfigHandler *handler = &general_handlers[i];
      void *member_ptr = (char*)config + handler->offset;
      if (action_taker(member_ptr, handler->action, value) == 0) {
        return 0;
      }
    }
  }
  (void)fprintf(stderr, "Unknown key in general section: %s\n", key);
  return -1;
}

int configure_source(Source *source, const char *key, const char *value) {
  for (int i = 0; source_handlers[i].key != NULL; i++) {
    if (strcmp(key, source_handlers[i].key) == 0) {
      const ConfigHandler *handler = &source_handlers[i];
      void *member_ptr = (char*)source + handler->offset;
      if (action_taker(member_ptr, handler->action, value) == 0) {
        return 0;
      }
    }
  }
  (void)fprintf(stderr, "Unknown key in source section: %s\n", key);
  return -1;
}

int configure_fan(Fan *fan, const char *key, const char *value) {
  for (int i = 0; fan_handlers[i].key != NULL; i++) {
    if (strcmp(key, fan_handlers[i].key) == 0) {
      const ConfigHandler *handler = &fan_handlers[i];
      void *member_ptr = (char*)fan + handler->offset;
      if (action_taker(member_ptr, handler->action, value) == 0) {
        return 0;
      }
    }
  }
  (void)fprintf(stderr, "Unknown key in source section: %s\n", key);
  return -1;
}

static int handler(void *user, const char *section, const char *name,
                   const char *value)
{
  Config *config = user;

  if (strcmp(section, "Source") == 0) {
    if (name == NULL && value == NULL) {
      config->current_section = create_object(
        (void**)&config->sources,
        &config->num_sources,
        &config->source_capacity,
        sizeof(Source)
      );
      if (config->current_section == NULL) {
        return 0;
      }
    }
    else {
      if (configure_source(config->current_section, name, value) < 0) {
        return 0;
      }
    }
  }
  else if (strcmp(section, "Fan") == 0) {
    if (name == NULL && value == NULL) {
      config->current_section = create_object(
        (void**)&config->fans,
        &config->num_fans,
        &config->fan_capacity,
        sizeof(Fan)
      );
      if (config->current_section == NULL) {
        return 0;
      }
    }
    else {
      if (configure_fan(config->current_section, name, value) < 0) {
        return 0;
      }
    }
  }
  else {
    if (configure_general(config, name, value) < 0) {
      return 0;
    }
  }

  return 1;
}

void free_config(Config *config) {
  free(config->graph_file);
  for (int i = 0; i < config->num_sources; i++) {
    free(config->sources[i].name);
    free(config->sources[i].driver);
    free(config->sources[i].pci_device);
    free(config->sources[i].sensors_string);
  }
  free(config->sources);
  for (int i = 0; i < config->num_fans; i++) {
    free(config->fans[i].name);
    free(config->fans[i].driver);
    free(config->fans[i].pwm_file);
    free(config->fans[i].temp_sources_string);
  }
  free(config->fans);
}

int load_graph(const char *graph_file, Graph *graph) {
  int ret = 0;
  FILE *file = fopen(graph_file, "r");
  if (file == NULL) {
    (void)fprintf(stderr, "Failed to open %s\n", graph_file);
    return -1;
  }

  char *line = NULL;
  size_t len = 0;
  ssize_t read;
  while ((read = getline(&line, &len, file)) != -1) {
    if (line[0] == '#' || line[0] == '\n') {
      continue;
    }

    if (graph->num_points == graph->capacity) {
      if (resize_array((void**)&graph->points, sizeof(GraphPoint), &graph->capacity) < 0) {
        return -1;
      }
    }

    char *graph_line = line;
    graph->points[graph->num_points].temp = strtof(graph_line, &graph_line);
    graph->points[graph->num_points].fan_speed = strtof(graph_line, NULL);
    graph->num_points++;
  }
  if (ferror(file) != 0) {
    perror("getline");
    ret = -1;
  }

  free(line);
  if (fclose(file) == EOF) {
    perror("fclose");
    ret = -1;
  }

  return ret;
}

int load_config(const char *path, Config *config) {
  memset(config, 0, sizeof(Config));
  int ret = ini_parse(path, handler, config);
  switch (ret) {
    case  0:
      break;
    case -1:
      (void)fprintf(stderr, "Can't load %s\n", path);
      free_config(config);
      return -1;
    default:
      (void)fprintf(stderr, "Error in config file at line %i\n", ret);
      free_config(config);
      return -1;
  }

  return 0;
}
