#include "sixlift/platform.h"

#if !defined(_WIN32)

#include "sixlift/log.h"
#include "sixlift/proc.h"
#include "sixlift/route.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define BIN      "/usr/local/bin/sixlift"
#define STATEDIR "/var/lib/sixlift"
#define STATEF   "/var/lib/sixlift/enabled"
#define LOGF     "/var/log/sixlift.log"
#define SERVICE  "/etc/systemd/system/sixlift-watchdog.service"
#define TIMER    "/etc/systemd/system/sixlift-watchdog.timer"
#define TIMER_UNIT "sixlift-watchdog.timer"

const char *plat_name(void)       { return "Linux"; }
const char *plat_bin_path(void)   { return BIN; }
const char *plat_state_path(void) { return STATEF; }
const char *plat_state_dir(void)  { return STATEDIR; }
const char *plat_log_path(void)   { return LOGF; }

int plat_is_admin(void) { return geteuid() == 0; }

int plat_self_path(char *buf, size_t buflen)
{
    ssize_t r = readlink("/proc/self/exe", buf, buflen - 1);
    if (r < 0)
        return -1;
    buf[r] = '\0';
    return 0;
}

int plat_mkdir(const char *path)
{
    if (mkdir(path, 0755) == 0)
        return 0;
    return plat_exists(path) ? 0 : -1;
}

int plat_exists(const char *path) { return access(path, F_OK) == 0; }

int plat_copy_exec(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in)
        return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    char b[8192];
    size_t n;
    int rc = 0;
    while ((n = fread(b, 1, sizeof(b), in)) > 0)
        if (fwrite(b, 1, n, out) != n) {
            rc = -1;
            break;
        }
    fclose(in);
    fclose(out);
    if (rc == 0)
        chmod(dst, 0755);
    return rc;
}

/* First active Wi-Fi or Ethernet NetworkManager connection. */
int plat_active_connection(char *conn, size_t conn_len, char *dev, size_t dev_len)
{
    char out[4096];
    char *argv[] = {"nmcli", "-t", "-f", "NAME,DEVICE,TYPE",
                    "connection", "show", "--active", NULL};
    if (proc_capture(argv, out, sizeof(out)) != 0)
        return -1;

    char *save = NULL;
    for (char *line = strtok_r(out, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char *c1 = strchr(line, ':');
        if (!c1)
            continue;
        char *c2 = strchr(c1 + 1, ':');
        if (!c2)
            continue;
        *c1 = *c2 = '\0';
        if (strstr(c2 + 1, "wireless") || strstr(c2 + 1, "ethernet")) {
            snprintf(conn, conn_len, "%s", line);
            snprintf(dev, dev_len, "%s", c1 + 1);
            return 0;
        }
    }
    return -1;
}

int plat_set_dns64(const char *conn, const char *dev,
                   const char *const *servers, int count)
{
    (void)dev;
    char dns[512];
    size_t off = 0;
    dns[0] = '\0';
    for (int i = 0; i < count; i++) {
        int w = snprintf(dns + off, sizeof(dns) - off, "%s%s",
                         i ? " " : "", servers[i]);
        if (w < 0 || (size_t)w >= sizeof(dns) - off)
            break;
        off += (size_t)w;
    }
    LOG_DEBUGF("setting ipv6.dns on '%s' to: %s", conn, dns);
    char *m1[] = {"nmcli", "connection", "modify", (char *)conn,
                  "ipv6.dns", dns, NULL};
    char *m2[] = {"nmcli", "connection", "modify", (char *)conn,
                  "ipv6.ignore-auto-dns", "yes", NULL};
    char *up[] = {"nmcli", "connection", "up", (char *)conn, NULL};
    proc_run_quiet(m1);
    proc_run_quiet(m2);
    proc_run_quiet(up);
    sleep(2);
    return 0;
}

int plat_clear_dns(const char *conn, const char *dev)
{
    (void)dev;
    char *m1[] = {"nmcli", "connection", "modify", (char *)conn,
                  "ipv6.dns", "", NULL};
    char *m2[] = {"nmcli", "connection", "modify", (char *)conn,
                  "ipv6.ignore-auto-dns", "default", NULL};
    char *up[] = {"nmcli", "connection", "up", (char *)conn, NULL};
    proc_run_quiet(m1);
    proc_run_quiet(m2);
    proc_run_quiet(up);
    return 0;
}

int plat_reconnect(const char *conn)
{
    char *up[] = {"nmcli", "connection", "up", (char *)conn, NULL};
    int rc = proc_run_quiet(up);
    sleep(4);
    return rc;
}

int plat_remove_ipv6_blackhole(void)
{
    return route_delete_ipv6_blackhole_local();
}

int plat_watchdog_install(const char *self_bin)
{
    char svc[1024];
    snprintf(svc, sizeof(svc),
             "[Unit]\n"
             "Description=sixlift watchdog (self-heals IPv4 over IPv6)\n"
             "After=network-online.target\n"
             "Wants=network-online.target\n\n"
             "[Service]\n"
             "Type=oneshot\n"
             "ExecStart=%s heal\n",
             self_bin);
    const char *tmr =
        "[Unit]\n"
        "Description=Run sixlift watchdog every 2 minutes\n\n"
        "[Timer]\n"
        "OnBootSec=1min\n"
        "OnUnitActiveSec=2min\n"
        "Unit=sixlift-watchdog.service\n\n"
        "[Install]\n"
        "WantedBy=timers.target\n";

    FILE *f = fopen(SERVICE, "w");
    if (!f) {
        LOG_ERRORF("cannot write unit '%s'", SERVICE);
        return -1;
    }
    fputs(svc, f);
    fclose(f);
    LOG_DEBUGF("wrote systemd unit '%s'", SERVICE);

    f = fopen(TIMER, "w");
    if (!f) {
        LOG_ERRORF("cannot write unit '%s'", TIMER);
        return -1;
    }
    fputs(tmr, f);
    fclose(f);
    LOG_DEBUGF("wrote systemd unit '%s'", TIMER);

    char *dr[] = {"systemctl", "daemon-reload", NULL};
    proc_run_quiet(dr);
    return 0;
}

int plat_watchdog_remove(void)
{
    remove(SERVICE);
    remove(TIMER);
    char *dr[] = {"systemctl", "daemon-reload", NULL};
    proc_run_quiet(dr);
    return 0;
}

int plat_watchdog_set(int enabled)
{
    char *on[]  = {"systemctl", "enable", "--now", TIMER_UNIT, NULL};
    char *off[] = {"systemctl", "disable", "--now", TIMER_UNIT, NULL};
    return proc_run_quiet(enabled ? on : off);
}

int plat_watchdog_installed(void) { return plat_exists(TIMER); }

int plat_watchdog_active(void)
{
    char *act[] = {"systemctl", "is-active", "--quiet", TIMER_UNIT, NULL};
    return proc_run_quiet(act) == 0;
}

#endif /* !_WIN32 */
