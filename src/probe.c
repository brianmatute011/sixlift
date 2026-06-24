#include "sixlift/probe.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

/* Non-blocking connect with a wall-clock timeout. */
static int connect_timeout(int fd, const struct sockaddr *sa,
                           socklen_t salen, int sec)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -1;

    if (connect(fd, sa, salen) == 0)
        return 0;
    if (errno != EINPROGRESS)
        return -1;

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);
    struct timeval tv = {.tv_sec = sec, .tv_usec = 0};

    if (select(fd + 1, NULL, &wfds, NULL, &tv) <= 0)
        return -1; /* timed out or error */

    int soerr = 0;
    socklen_t len = sizeof(soerr);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len) < 0 || soerr != 0)
        return -1;

    return 0;
}

bool probe_tcp(const char *host, const char *port, int family, int timeout_sec)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    if (getaddrinfo(host, port, &hints, &res) != 0 || !res)
        return false;

    bool ok = false;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        if (connect_timeout(fd, ai->ai_addr, ai->ai_addrlen, timeout_sec) == 0) {
            ok = true;
            close(fd);
            break;
        }
        close(fd);
    }

    freeaddrinfo(res);
    return ok;
}

bool probe_nat64(void)
{
    /* ipv4.google.com is IPv4-only: an AAAA can only come from DNS64. */
    return probe_tcp("ipv4.google.com", "443", AF_INET6, 8);
}

bool probe_ipv6(void)
{
    /* Google Public DNS over IPv6 — a stable global IPv6 literal. */
    return probe_tcp("2001:4860:4860::8888", "53", AF_INET6, 6);
}
