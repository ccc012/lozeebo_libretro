/* log.h - Sistema de logging do nucleo Zeebo
 * Usa o callback de log do RetroArch quando disponivel,
 * senao cai para stderr.
 */
#ifndef ZEEBO_LOG_H
#define ZEEBO_LOG_H

#include <stdarg.h>

enum zlog_level {
    ZLOG_DEBUG = 0,
    ZLOG_INFO  = 1,
    ZLOG_WARN  = 2,
    ZLOG_ERROR = 3
};

/* Registrado pelo libretro_core.c com o retro_log_printf_t do frontend */
typedef void (*zlog_backend_t)(enum zlog_level level, const char *fmt, va_list ap);

void zlog_set_backend(zlog_backend_t backend);
/* Override minimo; env ZEEMU_LOG_LEVEL ganha se setada (re-lida aqui). */
void zlog_set_min_level(enum zlog_level level);
/* Core option mirror; ZEEMU_TRACE_TIMER env ganha se setada. */
void zlog_set_trace_timer(int enabled);
int zlog_trace_timer(void);
void zlog(enum zlog_level level, const char *fmt, ...);

#define LOGD(...) zlog(ZLOG_DEBUG, __VA_ARGS__)
#define LOGI(...) zlog(ZLOG_INFO,  __VA_ARGS__)
#define LOGW(...) zlog(ZLOG_WARN,  __VA_ARGS__)
#define LOGE(...) zlog(ZLOG_ERROR, __VA_ARGS__)

#endif /* ZEEBO_LOG_H */
