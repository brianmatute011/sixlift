#include "sixlift/proc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

/* Core runner. If buf != NULL, the child's stdout is captured into it.
 * Otherwise, if quiet, stdout/stderr are redirected to /dev/null. */
static int run_impl(char *const argv[], int quiet, char *buf, size_t buflen)
{
    int pipefd[2] = {-1, -1};

    if (buf && pipe(pipefd) != 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
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

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
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
