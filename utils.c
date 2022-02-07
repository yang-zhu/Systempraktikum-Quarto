#include <string.h>
#include <time.h>
#include <stdio.h>
#include "utils.h"

// Return a substring of a string from i (inclusive) to j (exclusive).
char *substring(char *string, size_t i, size_t j) {
    char *substr = malloc(j-i+1);
    memcpy(substr, &string[i], j-i);
    substr[j-i] = '\0';
    return substr;
}

// Sleep for required milliseconds.
void milliSleep(int milliseconds) {
    struct timespec sleepingTime;
    sleepingTime.tv_sec = milliseconds / 1000;  // seconds
    sleepingTime.tv_nsec = milliseconds % 1000 * 1000 * 1000;  // nanoseconds
    nanosleep(&sleepingTime, NULL);
}