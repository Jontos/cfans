#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
