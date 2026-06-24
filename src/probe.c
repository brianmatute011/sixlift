#include "sixlift/probe.h"

#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SL_BADSOCK INVALID_SOCKET
#define SL_CLOSE   closesocket
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define SL_BADSOCK (-1)
#define SL_CLOSE   close
#endif

/* One-time Winsock init (no-op elsewhere). */
static int net_init(void)
{
#if defined(_WIN32)
    static int done = 0;
    if (!done) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return -1;
        done = 1;
    }
#endif
    return 0;
}

static int set_nonblocking(sock_t fd)
{
#if defined(_WIN32)
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0)
        return -1;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
#endif
}

static int conn_in_progress(void)
{
#if defined(_WIN32)
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EINPROGRESS;
#endif
}

static int connect_timeout(sock_t fd, const struct sockaddr *sa,
                           socklen_t salen, int sec)
{
    if (set_nonblocking(fd) != 0)
        return -1;

    if (connect(fd, sa, salen) == 0)
        return 0;
    if (!conn_in_progress())
        return -1;

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);
    struct timeval tv = {sec, 0};

#if defined(_WIN32)
    int nfds = 0; /* ignored on Windows */
#else
    int nfds = (int)fd + 1;
#endif
    if (select(nfds, NULL, &wfds, NULL, &tv) <= 0)
        return -1;

    int soerr = 0;
    socklen_t len = sizeof(soerr);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&soerr, &len) != 0 ||
        soerr != 0)
        return -1;
    return 0;
}

bool probe_tcp(const char *host, const char *port, int family, int timeout_sec)
{
    if (net_init() != 0)
        return false;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    if (getaddrinfo(host, port, &hints, &res) != 0 || !res)
        return false;

    bool ok = false;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        sock_t fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd == SL_BADSOCK)
            continue;
        if (connect_timeout(fd, ai->ai_addr, (socklen_t)ai->ai_addrlen,
                            timeout_sec) == 0) {
            ok = true;
            SL_CLOSE(fd);
            break;
        }
        SL_CLOSE(fd);
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
