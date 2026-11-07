/* log.c - Implementacao do logging */
#include <stdio.h>
#include <stdlib.h>
#include "log.h"

static zlog_backend_t g_backend = NULL;
static int g_min_level = -1; /* -1 = nao inicializado */
static int g_trace_timer = 0;

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

void zlog_set_min_level(enum zlog_level level) {
    /* Env ganha: se ZEEMU_LOG_LEVEL setada, ignora option. */
    const char *v = getenv("ZEEMU_LOG_LEVEL");
    if (v && *v)
        g_min_level = default_min_level();
    else
        g_min_level = (int)level;
}

void zlog_set_trace_timer(int enabled) {
    g_trace_timer = enabled ? 1 : 0;
}

int zlog_trace_timer(void) {
    /* Env ganha. */
    if (getenv("ZEEMU_TRACE_TIMER") != NULL)
        return 1;
    return g_trace_timer;
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
