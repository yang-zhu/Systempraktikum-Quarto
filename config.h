#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    char *hostname;
    char *port;
    char *gamekind;
} config;

extern config conf;  // global variable

void setConfig(char *path, config *conf);
void configCleanUp(void);

#endif