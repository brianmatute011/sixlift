#include "sixlift/proc.h"

#include "sixlift/log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void join_argv(char *const argv[], char *buf, size_t n)
{
    size_t off = 0;
    buf[0] = '\0';
    for (int i = 0; argv[i]; i++) {
        int w = snprintf(buf + off, n - off, "%s%s", i ? " " : "", argv[i]);
        if (w < 0 || (size_t)w >= n - off)
            break;
        off += (size_t)w;
    }
}

/* Core runner. If buf != NULL, the child's stdout is captured into it.
 * Otherwise, if quiet, stdout/stderr are redirected to /dev/null. */
static int run_impl(char *const argv[], int quiet, char *buf, size_t buflen)
{
    char cmd[512];
    join_argv(argv, cmd, sizeof(cmd));
    LOG_DEBUGF("exec: %s", cmd);

    int pipefd[2] = {-1, -1};

    if (buf && pipe(pipefd) != 0) {
        LOG_ERRORF("pipe() failed for '%s': %s", cmd, strerror(errno));
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERRORF("fork() failed for '%s': %s", cmd, strerror(errno));
        if (buf) { close(pipefd[0]); close(pipefd[1]); }
        return -1;
    }

    if (pid == 0) {
        /* Child. */
        if (buf) {
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[0]);
            close(pipefd[1]);
        } else if (quiet) {
            int dn = open("/dev/null", O_WRONLY);
            if (dn >= 0) {
                dup2(dn, STDOUT_FILENO);
                dup2(dn, STDERR_FILENO);
                if (dn > STDERR_FILENO)
                    close(dn);
            }
        }
        execvp(argv[0], argv);
        _exit(127); /* exec failed */
    }

    /* Parent. */
    if (buf) {
        close(pipefd[1]);
        size_t off = 0;
        ssize_t n;
        while (off + 1 < buflen &&
               (n = read(pipefd[0], buf + off, buflen - 1 - off)) > 0)
            off += (size_t)n;
        buf[off] = '\0';
        close(pipefd[0]);
    }

    int status;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code == 127)
            LOG_ERRORF("exec failed (command not found?): %s", cmd);
        else
            LOG_DEBUGF("exit %d: %s", code, cmd);
        return code;
    }
    LOG_WARNF("abnormal termination: %s", cmd);
    return -1;
}

int proc_run(char *const argv[])
{
    return run_impl(argv, 0, NULL, 0);
}

int proc_run_quiet(char *const argv[])
{
    return run_impl(argv, 1, NULL, 0);
}

int proc_capture(char *const argv[], char *buf, size_t buflen)
{
    if (!buf || buflen == 0)
        return -1;
    return run_impl(argv, 0, buf, buflen);
}
