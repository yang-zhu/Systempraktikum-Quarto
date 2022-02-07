#ifndef UTILS_H
#define UTILS_H

#include <stdlib.h>

typedef struct {
    char str[16];
} string16;

char *substring(char *string, size_t i, size_t j);
void milliSleep(int milliseconds);

#endif