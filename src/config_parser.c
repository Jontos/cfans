#include <ctype.h>
#include <ini.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config_parser.h"

#define INITIAL_CAPACITY 8

typedef struct {
  char *section;
  char *name;
  char *value;
} ConfigEntry;

typedef struct {
  ConfigEntry *entries;
  int count;
  int capacity;
} ConfigData;

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

ParsedSection parse_section(char *section) {
  ParsedSection result = { .type = SECTION_UNKNOWN, .name = NULL };

  char *delimiter = strchr(section, ':');

  if (delimiter == NULL) {
    if (strcmp(section, "General") == 0) {
      result.type = SECTION_GENERAL;
    }
    return result;
  }

  *delimiter = '\0';
  char *name = delimiter++;
  while (*name && isspace(*name)) {
    name++;
  }

  if (strcmp(section, "Source") == 0) {
    result.type = SECTION_SOURCE;
    result.name = name;
  }
  else if (strcmp(section, "Fan") == 0) {
    result.type = SECTION_FAN;
    result.name = name;
  }

  return result;
}

int handle_general_section(Config *config, ConfigEntry entry) {
  if (strcmp(entry.name, "Graph") == 0) {
    config->graph_file = strdup(entry.value);
  }
  else if (strcmp(entry.name, "Average") == 0) {
    config->average = (int)strtol(entry.value, NULL, 0);
  }
  else if (strcmp(entry.name, "Interval") == 0) {
    config->interval = (int)strtol(entry.value, NULL, 0);
  }
  else {
    (void)fprintf(stderr, "Unknown key \"%s\" in section \"%s\"\n", entry.name, entry.section);
    return -1;
  }
  return 0;
}

int resize_array(void **array, size_t size, int *capacity) {
  int new_capacity = *capacity == 0 ? INITIAL_CAPACITY : *capacity * 2;

  void *new_array = realloc(*array, size * new_capacity);
  if (new_array == NULL) {
    perror("Realloc failed");
    return -1;
  }

  *array = new_array;
  *capacity = new_capacity;
  return 0;
}

int handle_source_section(Config *config, ConfigEntry entry, const char *source_name) {
  // Search for existing source
  int source_num = -1;
  for (int i = 0; i < config->num_sources; i++) {
    if (strcmp(config->sources[i].name, source_name) == 0) {
      source_num = i;
      break;
    }
  }

  // If source doesn't exist
  if (source_num == -1) {
    // Check if we have space
    if (config->num_sources == config->source_capacity) {
      if (resize_array((void**)&config->sources, sizeof(Source), &config->source_capacity) < 0) {
        return -1;
      }
    }
    // Assign name to the new source
    source_num = config->num_sources;
    config->sources[config->num_sources].name = strdup(source_name);
    config->num_sources++;
  }

  Source *current_source = &config->sources[source_num];

  if (strcmp("Driver", entry.name) == 0) {
    current_source->driver = strdup(entry.value);
  }
  else if (strcmp("PciDevice", entry.name) == 0) {
    current_source->pci_device = strdup(entry.value);
  }
  else if (strcmp("Sensors", entry.name) == 0) {
    current_source->sensors_string = strdup(entry.value);
  }
  else {
    (void)fprintf(stderr, "Unknown key \"%s\" in section \"%s\"\n", entry.name, entry.section);
    return -1;
  }
  return 0;
}

int handle_fan_section(Config *config, ConfigEntry entry, const char *fan_name) {
  // Search for existing fan
  int fan_num = -1;
  for (int i = 0; i < config->num_fans; i++) {
    if (strcmp(config->fans[i].name, fan_name) == 0) {
      fan_num = i;
      break;
    }
  }

  // If fan doesn't exist
  if (fan_num == -1) {
    // Check if we have space
    if (config->num_fans == config->fan_capacity) {
      if (resize_array((void**)&config->fans, sizeof(Fan), &config->fan_capacity) < 0) {
        return -1;
      }
    }
    // Assign name to the new fan
    fan_num = config->num_fans;
    config->fans[config->num_fans].name = strdup(fan_name);
    config->num_fans++;
  }

  Fan *current_fan = &config->fans[fan_num];

  if (strcmp("Driver", entry.name) == 0) {
    current_fan->driver = strdup(entry.value);
  }
  else if (strcmp("PWMFile", entry.name) == 0) {
    current_fan->pwm_file = strdup(entry.value);
  }
  else if (strcmp("MinPWM", entry.name) == 0) {
    current_fan->min_pwm = (int)strtol(entry.value, NULL, 0);
  }
  else if (strcmp("MaxPWM", entry.name) == 0) {
    current_fan->max_pwm = (int)strtol(entry.value, NULL, 0);
  }
  else if (strcmp("TempSources", entry.name) == 0) {
    current_fan->temp_sources_string = strdup(entry.value);
  }
  else {
    (void)fprintf(stderr, "Unknown key \"%s\" in section \"%s\"\n", entry.name, entry.section);
    return -1;
  }
  return 0;
}

int process_config_data(Config *config, ConfigData *data) {
  for (int i = 0; i < data->count; i++) {
    ParsedSection psection = parse_section(data->entries[i].section);

    switch (psection.type) {
      case SECTION_GENERAL:
        if (handle_general_section(config, data->entries[i]) < 0) {
          return -1;
        }
        break;
      case SECTION_SOURCE:
        if (handle_source_section(config, data->entries[i], psection.name) < 0) {
          return -1;
        }
        break;
      case SECTION_FAN:
        if (handle_fan_section(config, data->entries[i], psection.name) < 0) {
          return -1;
        }
        break;
      case SECTION_UNKNOWN:
        (void)fprintf(stderr, "Config entry \"%s\" missing section!\n", data->entries->name);
        break;
    }
  }
  return 0;
}

static int handler(void *user, const char *section, const char *name,
                   const char *value)
{
  ConfigData *data = user;

  if (data->count == data->capacity) {
    if (resize_array((void**)&data->entries, sizeof(ConfigEntry), &data->capacity) < 0) {
      return 0;
    }
  }

  ConfigEntry *new_entry = &data->entries[data->count];

  new_entry->section = strdup(section);
  new_entry->name = strdup(name);
  new_entry->value = strdup(value);

  if (new_entry->section == NULL || new_entry->name == NULL || new_entry->value == NULL) {
    free(new_entry->section);
    free(new_entry->name);
    free(new_entry->value);
    perror("strdup failed");
    return 0;
  }

  data->count++;

  return 1;
}

void free_config_data(ConfigData *data) {
  for (int i = 0; i < data->count; i++) {
    free(data->entries[i].section);
    free(data->entries[i].name);
    free(data->entries[i].value);
  }
  free(data->entries);
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

int load_config(const char *path, Config *config) {
  memset(config, 0, sizeof(Config));

  ConfigData config_data;
  memset(&config_data, 0, sizeof(ConfigData));

  int ret = ini_parse(path, handler, &config_data);
  switch (ret) {
    case  0:
      break;
    case -1:
      (void)fprintf(stderr, "Can't load %s\n", path);
      free_config_data(&config_data);
      return -1;
    case -2:
      (void)fprintf(stderr, "ini_parse() failed to allocate memory!\n");
      free_config_data(&config_data);
      return -1;
    default:
      (void)fprintf(stderr, "Error in config file at line %i\n", ret);
      free_config_data(&config_data);
      return -1;
  }

  if (process_config_data(config, &config_data) < 0) {
    (void)fprintf(stderr, "Failed to process config data!\n");
    free_config_data(&config_data);
    free_config(config);
    return -1;
  }
  free_config_data(&config_data);

  return 0;
}
