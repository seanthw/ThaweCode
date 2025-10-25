#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

// A helper function to trim leading/trailing whitespace from a string
char *trim_whitespace(char *str) {
    char *end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = 0;
    return str;
}

// The main parsing function for a single line
void parse_config_line(char *line) {
    char *comment = strchr(line, '#');
    if (comment) *comment = '\0';

    char *separator = strchr(line, '=');
    if (separator == NULL) return;

    *separator = '\0';
    char *key = trim_whitespace(line);
    char *value = trim_whitespace(separator + 1);

    if (strcmp(key, "tab-stop") == 0) {
        E.tab_stop = atoi(value);
        if (E.tab_stop <= 0) E.tab_stop = 8;
    } else if (strcmp(key, "quit-times") == 0) {
        E.quit_times = atoi(value);
        if (E.quit_times <= 0) E.quit_times = 3;
    }
}

void load_config() {
    char path[1024];
    char *home = getenv("HOME");
    if (home == NULL) return;

    strcpy(path, home);
    strcat(path, "/.kilorc");

    FILE *fp = fopen(path, "r");
    if (fp == NULL) return;

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        parse_config_line(line);
    }

    free(line);
    fclose(fp);
}
