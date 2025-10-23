#include <ctype.h>
#include <ini.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config_parser.h"
#include "utils.h"

typedef enum {
  SECTION_UNKNOWN,
  SECTION_GENERAL,
  SECTION_SOURCE,
  SECTION_FAN
} SectionType;

typedef struct {
  SectionType type;
  const char *name;
} ParsedSection;

ParsedSection parse_section(const char *section) {
  ParsedSection result = { .type = SECTION_UNKNOWN, .name = NULL };

  char *delimiter = strchr(section, ':');
  if (delimiter == NULL) {
    if (strcmp(section, "General") == 0) {
      result.type = SECTION_GENERAL;
    }
    return result;
  }

  char *name = delimiter + 1;
  while (*name && isspace(*name)) {
    name++;
  }

  if (strncmp(section, "Source", strlen("Source")) == 0) {
    result.type = SECTION_SOURCE;
    result.name = name;
  }
  else if (strncmp(section, "Fan", strlen("Fan")) == 0) {
    result.type = SECTION_FAN;
    result.name = name;
  }

  return result;
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

Source *find_or_create_source(Source **sources, int *count,
                            int *capacity, const char *name)
{
  for (int i = 0; i < *count; i++) {
    if (strcmp((*sources)[i].name, name) == 0) {
      return &(*sources)[i];
    }
  }

  if (*count == *capacity) {
    if (resize_array((void**)sources, sizeof(Source), capacity) < 0) {
      return NULL;
    }
  }

  Source *new_source = &(*sources)[*count];

  memset(new_source, 0, sizeof(Source));
  new_source->name = strdup(name);
  if (new_source->name == NULL) {
    perror("strdup failed for new source name");
    return NULL;
  }
  (*count)++;
  return new_source;
}

Fan *find_or_create_fan(Fan **fans, int *count,
                            int *capacity, const char *name)
{
  for (int i = 0; i < *count; i++) {
    if (strcmp((*fans)[i].name, name) == 0) {
      return &(*fans)[i];
    }
  }

  if (*count == *capacity) {
    if (resize_array((void**)fans, sizeof(Fan), capacity) < 0) {
      return NULL;
    }
  }

  Fan *new_fan = &(*fans)[*count];

  memset(new_fan, 0, sizeof(Fan));
  new_fan->name = strdup(name);
  if (new_fan->name == NULL) {
    perror("strdup failed for new fan name");
    return NULL;
  }
  (*count)++;
  return new_fan;
}

int configure_source(Source *source, const char *key, const char *value) {
  if (strcmp("Driver", key) == 0) {
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
    source->scale = (int)strtol(value, NULL, 0);
  }
  else {
    (void)fprintf(stderr, "Unknown key in source section: %s\n", key);
    return -1;
  }
  return 0;
}

int configure_fan(Fan *fan, const char *key, const char *value) {
  if (strcmp("Driver", key) == 0) {
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

  ParsedSection psection = parse_section(section);

  switch (psection.type) {
    case SECTION_GENERAL:
      if (configure_general(config, name, value) < 0) {
        return 0;
      }
      break;
    case SECTION_SOURCE:
      Source *current_source = find_or_create_source(
        &config->sources,
        &config->num_sources,
        &config->source_capacity,
        psection.name
      );
      if (current_source == NULL) {
        return 0;
      }
      if (configure_source(current_source, name, value) < 0) {
        return 0;
      }
      break;
    case SECTION_FAN:
      Fan *current_fan = find_or_create_fan(
        &config->fans,
        &config->num_fans,
        &config->fan_capacity,
        psection.name
      );
      if (current_fan == NULL) {
        return 0;
      }
      if (configure_fan(current_fan, name, value) < 0) {
        return 0;
      }
      break;
    case SECTION_UNKNOWN:
      (void)fprintf(stderr, "Config entry \"%s\" missing section!\n", name);
      break;
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
  FILE *file = fopen(graph_file, "r");
  if (file == NULL) {
    (void)fprintf(stderr, "Failed to open %s\n", graph_file);
    return -1;
  }

  char *line = NULL;
  size_t len = 0;
  while (getline(&line, &len, file) != -1) {
    if (line[0] == '#' || line[0] == '\n') {
      continue;
    }

    if (graph->num_points >= graph->capacity) {
      if (resize_array((void**)&graph->fan_curve, sizeof(int[2]), &graph->capacity) < 0) {
        return -1;
      }
    }

    char *graph_line = line;
    graph->fan_curve[graph->num_points][0] = (int)strtol(graph_line, &graph_line, 0);
    graph->fan_curve[graph->num_points][1] = (int)strtol(graph_line, &graph_line, 0);
    graph->num_points++;
  }

  if (fclose(file) == EOF) {
    perror("fclose");
  }

  return 0;
}

int load_config(const char *path, Config *config) {
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
