/* log.c - Implementacao do logging */
#include <stdio.h>
#include <stdlib.h>
#include "log.h"

static zlog_backend_t g_backend = NULL;
static int g_min_level = -1; /* -1 = nao inicializado */

static int default_min_level(void) {
    /* ZEEMU_LOG_LEVEL define o minimo (0=DEBUG 1=INFO 2=WARN 3=ERROR;
     * padrao 0 = mostra tudo). Usado pelo smoke test para acelerar
     * runs longos silenciando LOGD/LOGI ruidosos por frame. */
    const char *v = getenv("ZEEMU_LOG_LEVEL");
    if (!v || !*v)
        return ZLOG_DEBUG;
    return atoi(v);
}

void zlog_set_backend(zlog_backend_t backend) {
    g_backend = backend;
}

void zlog(enum zlog_level level, const char *fmt, ...) {
    va_list ap;
    if (g_min_level < 0)
        g_min_level = default_min_level();
    if (level < g_min_level)
        return;
    va_start(ap, fmt);
    if (g_backend) {
        g_backend(level, fmt, ap);
    } else {
        static const char *names[] = { "DEBUG", "INFO", "WARN", "ERROR" };
        fprintf(stderr, "[LOZeebo %s] ", names[level]);
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
    }
    va_end(ap);
}
