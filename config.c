#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <string.h>
#include "utils.h"
#include "config.h"

config conf;

void setConfig(char *path, config *conf) {
    // register the clean up function to free all the fields of conf at all exit()
    atexit(configCleanUp);

    conf->hostname = NULL;
    conf->port = NULL;
    conf->gamekind = NULL;

    FILE *configFile = fopen(path, "r");
    if (!configFile) {
        perror("Error in opening the config file");
        exit(EXIT_FAILURE);
    }

    bool hasError = false;
    size_t bufCap = 0;
    char *buf = NULL;
    ssize_t readChars;
    while ((readChars = getline(&buf, &bufCap, configFile)) > 0) {
        // replace the second last character with '\0' only when it is '\n' (the last line of the file does not contain '\n')
        if (buf[strlen(buf)-1] == '\n') buf[strlen(buf)-1] = '\0';
        int readTillParamValue = 0;
        if (sscanf(buf, "Hostname = %n", &readTillParamValue), readTillParamValue != 0) {  // ',' operator creates a sequence point between the two operations, the first one is guaranteed to be exexuted before the second one
            conf->hostname = substring(buf, readTillParamValue, strlen(buf));  // strlen(buf) only counts till the first '\0' character
        } else if (sscanf(buf, "Port = %n", &readTillParamValue), readTillParamValue != 0) {
            conf->port = substring(buf, readTillParamValue, strlen(buf));
        } else if (sscanf(buf, "Gamekind = %n", &readTillParamValue), readTillParamValue != 0) {
            conf->gamekind = substring(buf, readTillParamValue, strlen(buf));
        } else {
            hasError = true;
            fprintf(stderr, "Error in processing configuration parameters: %s.\n", buf);
        }
    }

    free(buf);
    if (ferror(configFile)) {
        hasError = true;
        perror("Error in reading the config file.");
    }
    if (!conf->hostname) {
        hasError = true;
        fprintf(stderr, "Configuration parameter hostname is missing.\n");
    }
    if (conf->port == 0) {
        hasError = true;
        fprintf(stderr, "Configuration parameter port is missing.\n");
    }
    if (!conf->gamekind) {
        hasError = true;
        fprintf(stderr, "Configuration parameter gamekind is missing.\n");
    }

    fclose(configFile);
    if (hasError) exit(EXIT_FAILURE);
}

// free all the fields in conf
void configCleanUp(void) {
    free(conf.hostname);
    free(conf.port);
    free(conf.gamekind);
}