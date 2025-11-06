#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config_parser.h"
#include "utils.h"
#include "ini.h"

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

int configure_general(Config *config, const char *name, const char *value) {
  if (strcmp(name, "Graph") == 0) {
    config->graph_file = strdup(value);
  }
  else if (strcmp(name, "Average") == 0) {
    config->average = (int)strtol(value, NULL, 0);
  }
  else if (strcmp(name, "Interval") == 0) {
    config->interval = (int)strtol(value, NULL, 0);
  }
  else {
    (void)fprintf(stderr, "Unknown key in general section: %s\n", name);
    return -1;
  }
  return 0;
}

int configure_source(Source *source, const char *key, const char *value) {
  if (strcmp("Name", key) == 0) {
    source->name = strdup(value);
    if (source->name == NULL) {
      perror("strdup failed for name");
      return -1;
    }
  }
  else if (strcmp("Driver", key) == 0) {
    source->driver = strdup(value);
    if (source->driver == NULL) {
      perror("strdup failed for driver");
      return -1;
    }
  }
  else if (strcmp("PciDevice", key) == 0) {
    source->pci_device = strdup(value);
    if (source->pci_device == NULL) {
      perror("strdup failed for pci_device");
      return -1;
    }
  }
  else if (strcmp("Sensors", key) == 0) {
    source->sensors_string = strdup(value);
    if (source->sensors_string == NULL) {
      perror("strdup failed for sensors_string");
      return -1;
    }
  }
  else if (strcmp("Scale", key) == 0) {
    source->scale = strtof(value, NULL);
  }
  else {
    (void)fprintf(stderr, "Unknown key in source section: %s\n", key);
    return -1;
  }
  return 0;
}

int configure_fan(Fan *fan, const char *key, const char *value) {
  if (strcmp("Name", key) == 0) {
    fan->name = strdup(value);
    if (fan->name == NULL) {
      perror("strdup failed for name");
      return -1;
    }
  }
  else if (strcmp("Driver", key) == 0) {
    fan->driver = strdup(value);
    if (fan->driver == NULL) {
      perror("strdup failed for driver");
      return -1;
    }
  }
  else if (strcmp("PWMFile", key) == 0) {
    fan->pwm_file = strdup(value);
    if (fan->pwm_file == NULL) {
      perror("strdup failed for pwm_file");
      return -1;
    }
  }
  else if (strcmp("MinPWM", key) == 0) {
    fan->min_pwm = (int)strtol(value, NULL, 0);
  }
  else if (strcmp("MaxPWM", key) == 0) {
    fan->max_pwm = (int)strtol(value, NULL, 0);
  }
  else if (strcmp("TempSources", key) == 0) {
    fan->temp_sources_string = strdup(value);
    if (fan->temp_sources_string == NULL) {
      perror("strdup failed for temp_sources_string");
      return -1;
    }
  }
  else {
    (void)fprintf(stderr, "Unknown key in fan section: %s\n", key);
    return -1;
  }
  return 0;
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
    case -2:
      (void)fprintf(stderr, "ini_parse() failed to allocate memory!\n");
      free_config(config);
      return -1;
    default:
      (void)fprintf(stderr, "Error in config file at line %i\n", ret);
      free_config(config);
      return -1;
  }

  return 0;
}
