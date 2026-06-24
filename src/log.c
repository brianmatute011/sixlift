#include "sixlift/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(_WIN32)
#include <syslog.h>
#endif

static FILE       *g_file          = NULL;
static log_level_t g_file_level     = SL_DEBUG;
static log_level_t g_console_level  = SL_WARN;
static log_level_t g_syslog_level   = SL_INFO;
static int         g_use_syslog     = 0;

static const char *const LEVEL_NAME[SL_LEVEL_COUNT] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

int log_level_from_name(const char *name)
{
    if (!name)
        return -1;
    for (int i = 0; i < SL_LEVEL_COUNT; i++) {
#if defined(_WIN32)
        if (_stricmp(name, LEVEL_NAME[i]) == 0)
#else
        if (strcasecmp(name, LEVEL_NAME[i]) == 0)
#endif
            return i;
    }
    return -1;
}

void log_set_file_level(log_level_t level)    { g_file_level = level; }
void log_set_console_level(log_level_t level) { g_console_level = level; }
void log_set_syslog_level(log_level_t level)  { g_syslog_level = level; }

void log_enable_syslog(int enable)
{
#if !defined(_WIN32)
    g_use_syslog = enable;
#else
    (void)enable;
    g_use_syslog = 0;
#endif
}

void log_init(const char *file_path, const char *ident)
{
    const char *env = getenv("SIXLIFT_LOG_LEVEL");
    int lvl = log_level_from_name(env);
    if (lvl >= 0)
        g_file_level = (log_level_t)lvl;

    if (file_path)
        g_file = fopen(file_path, "a");

#if !defined(_WIN32)
    if (g_use_syslog)
        openlog(ident ? ident : "sixlift", LOG_PID, LOG_DAEMON);
#else
    (void)ident;
#endif
}

void log_shutdown(void)
{
    if (g_file) {
        fclose(g_file);
        g_file = NULL;
    }
#if !defined(_WIN32)
    if (g_use_syslog)
        closelog();
#endif
}

static const char *basename_of(const char *path)
{
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            b = p + 1;
    return b;
}

static void utc_timestamp(char *buf, size_t n)
{
    time_t t = time(NULL);
    struct tm tm_;
#if defined(_WIN32)
    gmtime_s(&tm_, &t);
#else
    gmtime_r(&t, &tm_);
#endif
    if (strftime(buf, n, "%Y-%m-%dT%H:%M:%SZ", &tm_) == 0)
        buf[0] = '\0';
}

#if !defined(_WIN32)
static int to_syslog_priority(log_level_t level)
{
    switch (level) {
    case SL_TRACE:
    case SL_DEBUG: return LOG_DEBUG;
    case SL_INFO:  return LOG_INFO;
    case SL_WARN:  return LOG_WARNING;
    case SL_ERROR: return LOG_ERR;
    case SL_FATAL: return LOG_CRIT;
    default:       return LOG_INFO;
    }
}
#endif

void log_emit(log_level_t level, const char *file, int line,
              const char *func, const char *fmt, ...)
{
    int to_file    = g_file && level >= g_file_level;
    int to_console = level >= g_console_level;
    int to_syslog  = g_use_syslog && level >= g_syslog_level;

    if (!to_file && !to_console && !to_syslog)
        return;

    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    const char *base = basename_of(file);
    const char *name = (level < SL_LEVEL_COUNT) ? LEVEL_NAME[level] : "?";

    if (to_file || to_console) {
        char ts[32];
        utc_timestamp(ts, sizeof(ts));
        if (to_file) {
            fprintf(g_file, "%s [%-5s] %s:%d %s: %s\n",
                    ts, name, base, line, func, msg);
            fflush(g_file);
        }
        if (to_console)
            fprintf(stderr, "%s [%-5s] %s:%d %s: %s\n",
                    ts, name, base, line, func, msg);
    }

#if !defined(_WIN32)
    if (to_syslog)
        syslog(to_syslog_priority(level), "%s:%d %s: %s",
               base, line, func, msg);
#endif
}
