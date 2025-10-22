#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

typedef struct {
    char *name;
    char *driver;
    char *pci_device;
    char *sensors_string;
    char **sensors;
    int num_sensors;
} Source;

typedef struct {
    char *name;
    char *driver;
    char *pwm_file;
    int min_pwm;
    int max_pwm;
    char *temp_sources_string;
    char **temp_sources;
    int num_temp_sources;
} Fan;

typedef struct {
    char *graph_file;
    int average;
    int interval;

    Source *sources;
    int num_sources;
    int source_capacity;

    Fan *fans;
    int num_fans;
    int fan_capacity;
} Config;

int load_config(const char *path, Config *config);
void free_config(Config *config);

#endif
