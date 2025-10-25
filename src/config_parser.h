#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

typedef struct {
    char *name;
    char *driver;
    char *pci_device;
    char *sensors_string;
    int scale;
} Source;

typedef struct {
    char *name;
    char *driver;
    char *pwm_file;
    int min_pwm;
    int max_pwm;
    char *temp_sources_string;
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

typedef struct {
    int temp;
    int fan_speed;
} GraphPoint;

typedef struct {
    GraphPoint *points;
    int num_points;
    int capacity;
} Graph;

int load_config(const char *path, Config *config);
int load_graph(const char *graph_file, Graph *graph);
void free_config(Config *config);

#endif
