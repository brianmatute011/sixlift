#ifndef SIXLIFT_CONFIG_H
#define SIXLIFT_CONFIG_H

#define SIXLIFT_VERSION "1.2.0"

/* Two independent public NAT64/DNS64 providers for redundancy:
 *   nat64.net (Kasper Dupont)  +  Trex (Tampere, Finland).
 * Reachable over IPv6; they synthesize AAAA records for IPv4-only names.
 * Installed paths are platform-specific — see platform.h. */
extern const char *const SIXLIFT_DNS64[];
extern const int SIXLIFT_DNS64_COUNT;

#endif /* SIXLIFT_CONFIG_H */
