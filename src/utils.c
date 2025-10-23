#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRING_BUFFER_SIZE 256
#define INITIAL_CAPACITY 4

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

char *line_from_file(const char *path) {
  FILE *filep = fopen(path, "r");
  if (filep == NULL) {
    (void)fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
    return NULL;
  }

  char *line = NULL;
  size_t len = 0;
  ssize_t nread = getline(&line, &len, filep);
  if (fclose(filep) == EOF) {
    (void)fprintf(stderr, "Failed to close %s: %s\n", path, strerror(errno));
    free(line);
    return NULL;
  }
  if (nread == -1) {
    free(line);
    return NULL;
  }
  if (nread > 0 && line[nread - 1] == '\n') {
    line[nread - 1] = '\0';
  }
  return line;
}

char *concat_string(const char *string1, const char *string2, char delimiter) {
  size_t buffer_size = STRING_BUFFER_SIZE;
  char *string_buffer = malloc(buffer_size);
  if (string_buffer == NULL) {
    perror("malloc");
    return NULL;
  }
  while (true) {
    int ret;
    if (delimiter == '\0') {
      ret = snprintf(string_buffer, buffer_size, "%s%s", string1, string2);
    }
    else {
      ret = snprintf(string_buffer, buffer_size, "%s%c%s", string1, delimiter, string2);
    }
    if (ret < 0) {
      return NULL;
    }
    if ((size_t)ret < buffer_size) {
      return string_buffer;
    }
    buffer_size = (size_t)ret + 1;
    char *new_buffer = realloc(string_buffer, buffer_size);
    if (new_buffer == NULL) {
      free(string_buffer);
      return NULL;
    }
    string_buffer = new_buffer;
  }
}
