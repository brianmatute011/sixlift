#ifndef SIXLIFT_PLATFORM_H
#define SIXLIFT_PLATFORM_H

#include <stddef.h>

/*
 * Platform abstraction. The core (service.c) is OS-independent and drives the
 * OS through this interface. One backend is compiled in per target:
 *   - platform_linux.c   : rtnetlink + NetworkManager + systemd
 *   - platform_windows.c : Winsock + netsh + Task Scheduler
 */

/* Short platform name, e.g. "Linux" or "Windows". */
const char *plat_name(void);

/* Installed filesystem locations (backend-specific absolute paths). */
const char *plat_bin_path(void);    /* where the binary is installed     */
const char *plat_state_path(void);  /* "enabled" marker file             */
const char *plat_state_dir(void);   /* directory holding state + log     */
const char *plat_log_path(void);    /* watchdog log                      */

/* Path of the running executable. Returns 0 on success. */
int plat_self_path(char *buf, size_t buflen);

/* Filesystem helpers (portable wrappers). */
int plat_mkdir(const char *path);          /* create dir (ok if exists)   */
int plat_exists(const char *path);         /* 1 if path exists            */
int plat_copy_exec(const char *src, const char *dst); /* copy + exec perms */

/* Privilege check. Returns 1 if the caller can change system network state. */
int plat_is_admin(void);

/* Active connection discovery.
 *   conn : opaque handle passed back to set/clear/reconnect
 *   dev  : interface id used for DNS operations / display
 * Returns 0 on success. */
int plat_active_connection(char *conn, size_t conn_len,
                           char *dev, size_t dev_len);

/* DNS control. */
int plat_set_dns64(const char *conn, const char *dev,
                   const char *const *servers, int count);
int plat_clear_dns(const char *conn, const char *dev);

/* Bounce the link to refresh addressing/routes. */
int plat_reconnect(const char *conn);

/* Remove any IPv6 blackhole-default route in the local table (a VPN/privacy
 * kill-switch leftover on Linux). Returns count removed; 0 where N/A. */
int plat_remove_ipv6_blackhole(void);

/* Watchdog lifecycle. `self_bin` runs as `<self_bin> heal` periodically. */
int plat_watchdog_install(const char *self_bin); /* create mechanism       */
int plat_watchdog_remove(void);                  /* tear down              */
int plat_watchdog_set(int enabled);              /* start/enable or stop   */
int plat_watchdog_installed(void);               /* 1 if installed         */
int plat_watchdog_active(void);                  /* 1 if running/scheduled */

#endif /* SIXLIFT_PLATFORM_H */
