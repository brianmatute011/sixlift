#ifndef SIXLIFT_CONFIG_H
#define SIXLIFT_CONFIG_H

#define SIXLIFT_VERSION "1.0.0"

/* Installed locations. */
#define SIXLIFT_BIN      "/usr/local/bin/sixlift"
#define SIXLIFT_SERVICE  "/etc/systemd/system/sixlift-watchdog.service"
#define SIXLIFT_TIMER    "/etc/systemd/system/sixlift-watchdog.timer"
#define SIXLIFT_STATEDIR "/var/lib/sixlift"
#define SIXLIFT_STATE    "/var/lib/sixlift/enabled"
#define SIXLIFT_LOG      "/var/log/sixlift.log"

/* Two independent public NAT64/DNS64 providers for redundancy:
 *   nat64.net (Kasper Dupont)  +  Trex (Tampere, Finland).
 * Reachable over IPv6; they synthesize AAAA records for IPv4-only names. */
extern const char *const SIXLIFT_DNS64[];
extern const int SIXLIFT_DNS64_COUNT;

#endif /* SIXLIFT_CONFIG_H */
