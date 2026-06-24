#ifndef SIXLIFT_PROBE_H
#define SIXLIFT_PROBE_H

#include <stdbool.h>

/* TCP-connect probe with a timeout. Resolves host with getaddrinfo()
 * restricted to `family`, then attempts a non-blocking connect.
 * Returns true if any resolved address connects within timeout_sec. */
bool probe_tcp(const char *host, const char *port, int family, int timeout_sec);

/* True if an IPv4-only host is reachable over IPv6 (proves the NAT64/DNS64
 * path end to end: the configured DNS64 must synthesize an AAAA, and the
 * NAT64 gateway must route it). */
bool probe_nat64(void);

/* True if the base IPv6 internet is reachable (TCP to a global IPv6 literal). */
bool probe_ipv6(void);

#endif /* SIXLIFT_PROBE_H */
