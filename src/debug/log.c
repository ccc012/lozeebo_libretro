/* log.c - Implementacao do logging */
#include <stdio.h>
#include "log.h"

static zlog_backend_t g_backend = NULL;

void zlog_set_backend(zlog_backend_t backend) {
    g_backend = backend;
}

void zlog(enum zlog_level level, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (g_backend) {
        g_backend(level, fmt, ap);
    } else {
        static const char *names[] = { "DEBUG", "INFO", "WARN", "ERROR" };
        fprintf(stderr, "[Zeebo %s] ", names[level]);
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
    }
    va_end(ap);
}
