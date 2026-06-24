#ifndef SIXLIFT_PROC_H
#define SIXLIFT_PROC_H

#include <stddef.h>

/* Run argv[0] with arguments via fork/execvp (no shell, no injection).
 * Returns the child's exit status, or -1 on fork/exec failure. */
int proc_run(char *const argv[]);

/* Same as proc_run but the child's stdout/stderr go to /dev/null. */
int proc_run_quiet(char *const argv[]);

/* Run argv and capture stdout into buf (NUL-terminated, truncated to buflen).
 * Returns the child's exit status, or -1 on failure. */
int proc_capture(char *const argv[], char *buf, size_t buflen);

#endif /* SIXLIFT_PROC_H */
