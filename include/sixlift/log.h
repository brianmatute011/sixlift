#ifndef SIXLIFT_LOG_H
#define SIXLIFT_LOG_H

/*
 * Leveled, multi-sink logging facility.
 *
 * Sinks, each with an independent threshold:
 *   - file   : detailed, append-only diagnostics (default DEBUG)
 *   - stderr : human-facing warnings/errors      (default WARN)
 *   - syslog : system journal on POSIX           (default INFO; off on Windows)
 *
 * Records carry an RFC 3339 UTC timestamp, severity, and source location
 * (file:line function). Output is plain ASCII - no colors, no emoji.
 *
 * Level names are prefixed SL_ to avoid clashing with the LOG_* macros that
 * <syslog.h> defines.
 */

typedef enum {
    SL_TRACE = 0,
    SL_DEBUG,
    SL_INFO,
    SL_WARN,
    SL_ERROR,
    SL_FATAL,
    SL_LEVEL_COUNT
} log_level_t;

/* Open the log file (append) and read SIXLIFT_LOG_LEVEL from the environment.
 * Safe to call once at startup; a NULL or unopenable path disables the file
 * sink without failing. */
void log_init(const char *file_path, const char *ident);

/* Release resources (closes the file and syslog). */
void log_shutdown(void);

/* Per-sink threshold control. */
void log_set_file_level(log_level_t level);
void log_set_console_level(log_level_t level);
void log_set_syslog_level(log_level_t level);
void log_enable_syslog(int enable);

/* Parse a level name ("trace".."fatal"); returns -1 if unknown. */
int log_level_from_name(const char *name);

/* Core entry point - use the macros below instead. */
void log_emit(log_level_t level, const char *file, int line,
              const char *func, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 5, 6)))
#endif
    ;

#define LOG_TRACEF(...) log_emit(SL_TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_DEBUGF(...) log_emit(SL_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_INFOF(...)  log_emit(SL_INFO,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_WARNF(...)  log_emit(SL_WARN,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_ERRORF(...) log_emit(SL_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_FATALF(...) log_emit(SL_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)

#endif /* SIXLIFT_LOG_H */
