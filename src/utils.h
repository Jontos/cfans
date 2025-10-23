#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>

int resize_array(void **array, size_t size, int *capacity);
char *line_from_file(const char *path);
char *concat_string(const char *string1, const char *string2, char delimiter);

#endif
