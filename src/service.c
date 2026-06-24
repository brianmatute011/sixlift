#include "sixlift/service.h"

#include "sixlift/config.h"
#include "sixlift/probe.h"
#include "sixlift/proc.h"
#include "sixlift/route.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

/* Upper bound on DNS64 servers passed to resolvectl in one call. */
#define MAX_DNS 16

/* ---- small helpers ---------------------------------------------------- */

static void wlog(const char *fmt, ...)
{
    FILE *f = fopen(SIXLIFT_LOG, "a");
    if (!f)
        return;
    time_t t = time(NULL);
    struct tm tm;
    char ts[32] = "";
    if (localtime_r(&t, &tm))
        strftime(ts, sizeof(ts), "%F %T", &tm);
    fprintf(f, "%s ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static int require_root(const char *action)
{
    if (geteuid() != 0) {
        fprintf(stderr, "This needs root. Try:  sudo sixlift %s\n", action);
        return -1;
    }
    return 0;
}

static int exists(const char *path)
{
    return access(path, F_OK) == 0;
}

/* Discover the active NetworkManager connection (name + device).
 * Picks the first active Wi-Fi or Ethernet connection. */
static int get_active(char *name, size_t nn, char *dev, size_t nd)
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
        *c1 = '\0';
        *c2 = '\0';
        const char *nm = line, *dv = c1 + 1, *tp = c2 + 1;
        if (strstr(tp, "wireless") || strstr(tp, "ethernet")) {
            snprintf(name, nn, "%s", nm);
            snprintf(dev, nd, "%s", dv);
            return 0;
        }
    }
    return -1;
}

static void join_dns64(char *buf, size_t n)
{
    size_t off = 0;
    buf[0] = '\0';
    for (int i = 0; i < SIXLIFT_DNS64_COUNT; i++) {
        int w = snprintf(buf + off, n - off, "%s%s",
                         i ? " " : "", SIXLIFT_DNS64[i]);
        if (w < 0 || (size_t)w >= n - off)
            break;
        off += (size_t)w;
    }
}

static void nm_set_dns64(const char *name)
{
    char dns[512];
    join_dns64(dns, sizeof(dns));
    char *m1[] = {"nmcli", "connection", "modify", (char *)name,
                  "ipv6.dns", dns, NULL};
    char *m2[] = {"nmcli", "connection", "modify", (char *)name,
                  "ipv6.ignore-auto-dns", "yes", NULL};
    char *up[] = {"nmcli", "connection", "up", (char *)name, NULL};
    proc_run_quiet(m1);
    proc_run_quiet(m2);
    proc_run_quiet(up);
}

static void nm_clear_dns(const char *name)
{
    char *m1[] = {"nmcli", "connection", "modify", (char *)name,
                  "ipv6.dns", "", NULL};
    char *m2[] = {"nmcli", "connection", "modify", (char *)name,
                  "ipv6.ignore-auto-dns", "default", NULL};
    char *up[] = {"nmcli", "connection", "up", (char *)name, NULL};
    proc_run_quiet(m1);
    proc_run_quiet(m2);
    proc_run_quiet(up);
}

static int copy_file(const char *src, const char *dst, mode_t mode)
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
    while ((n = fread(b, 1, sizeof(b), in)) > 0) {
        if (fwrite(b, 1, n, out) != n) {
            rc = -1;
            break;
        }
    }
    fclose(in);
    fclose(out);
    if (rc == 0)
        chmod(dst, mode);
    return rc;
}

static int write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fputs(content, f);
    fclose(f);
    return 0;
}

/* ---- subcommands ------------------------------------------------------ */

int sl_test(void)
{
    printf("Testing IPv4-only host (ipv4.google.com) over IPv6...\n");
    if (probe_nat64()) {
        printf("  OK: IPv4 is reaching the internet over IPv6.\n");
        return 0;
    }
    printf("  FAIL: the NAT64 path is not working.\n");
    if (!probe_ipv6())
        printf("  Also: base IPv6 is currently down on this machine.\n");
    return 1;
}

int sl_on(void)
{
    if (require_root("on"))
        return 1;

    char name[256], dev[64];
    if (get_active(name, sizeof(name), dev, sizeof(dev)) != 0) {
        fprintf(stderr, "No active network connection found.\n");
        return 1;
    }

    printf("Enabling NAT64/DNS64 on connection: %s\n", name);
    mkdir(SIXLIFT_STATEDIR, 0755);
    FILE *f = fopen(SIXLIFT_STATE, "w");
    if (f)
        fclose(f);

    nm_set_dns64(name);

    if (exists(SIXLIFT_TIMER)) {
        char *en[] = {"systemctl", "enable", "--now",
                      "sixlift-watchdog.timer", NULL};
        if (proc_run_quiet(en) == 0)
            printf("Watchdog enabled (self-healing every 2 min).\n");
    } else {
        printf("Watchdog not installed (optional): sudo sixlift install\n");
    }

    printf("DNS64 applied and connection reloaded.\n\n");
    return sl_test();
}

int sl_off(void)
{
    if (require_root("off"))
        return 1;

    char name[256], dev[64];
    int have = get_active(name, sizeof(name), dev, sizeof(dev)) == 0;

    printf("Disabling NAT64/DNS64 and reverting to normal...\n");
    unlink(SIXLIFT_STATE);

    if (exists(SIXLIFT_TIMER)) {
        char *dis[] = {"systemctl", "disable", "--now",
                       "sixlift-watchdog.timer", NULL};
        if (proc_run_quiet(dis) == 0)
            printf("  - watchdog stopped\n");
    }
    if (have) {
        nm_clear_dns(name);
        printf("  - DNS of '%s' restored to the router's\n", name);
    }
    printf("Done. Everything back to normal.\n");
    return 0;
}

int sl_status(void)
{
    char name[256] = "?", dev[64] = "?";
    get_active(name, sizeof(name), dev, sizeof(dev));

    printf("sixlift %s — status\n", SIXLIFT_VERSION);
    printf("Active connection : %s (device %s)\n", name, dev);
    printf("Enabled           : %s\n", exists(SIXLIFT_STATE) ? "YES" : "no");
    printf("Command installed : %s\n", exists(SIXLIFT_BIN) ? "yes" : "no");

    if (exists(SIXLIFT_TIMER)) {
        char *act[] = {"systemctl", "is-active", "--quiet",
                       "sixlift-watchdog.timer", NULL};
        printf("Watchdog          : %s\n",
               proc_run_quiet(act) == 0 ? "installed & active"
                                        : "installed (stopped)");
    } else {
        printf("Watchdog          : not installed\n");
    }
    printf("\n");
    return sl_test();
}

int sl_heal(void)
{
    if (!exists(SIXLIFT_STATE)) /* sixlift is off — do nothing */
        return 0;
    if (probe_nat64()) /* healthy — nothing to do */
        return 0;

    wlog("NAT64 not responding — attempting repair");

    int del = route_delete_ipv6_blackhole_local();
    if (del > 0)
        wlog("  - removed %d IPv6 blackhole route(s) from local table", del);

    char name[256], dev[64];
    int have = get_active(name, sizeof(name), dev, sizeof(dev)) == 0;

    if (!probe_ipv6() && have) {
        wlog("  - base IPv6 down; reconnecting '%s'", name);
        char *up[] = {"nmcli", "connection", "up", name, NULL};
        proc_run_quiet(up);
        sleep(4);
    }

    if (have) {
        char *argv[3 + MAX_DNS + 1];
        int a = 0;
        argv[a++] = "resolvectl";
        argv[a++] = "dns";
        argv[a++] = dev;
        for (int i = 0; i < SIXLIFT_DNS64_COUNT && i < MAX_DNS; i++)
            argv[a++] = (char *)SIXLIFT_DNS64[i];
        argv[a] = NULL;
        proc_run_quiet(argv);

        char *dom[] = {"resolvectl", "domain", dev, "~.", NULL};
        proc_run_quiet(dom);
    }

    sleep(3);
    if (probe_nat64())
        wlog("  OK repaired");
    else
        wlog("  FAIL still down (Digicel IPv6 down, or both NAT64 providers down?)");
    return 0;
}

int sl_log(void)
{
    char *argv[] = {"tail", "-n", "40", SIXLIFT_LOG, NULL};
    if (proc_run(argv) != 0)
        printf("No log yet (%s).\n", SIXLIFT_LOG);
    return 0;
}

int sl_install(void)
{
    if (require_root("install"))
        return 1;

    printf("Installing sixlift %s...\n", SIXLIFT_VERSION);

    char self[4096];
    ssize_t r = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (r < 0) {
        perror("readlink");
        return 1;
    }
    self[r] = '\0';

    if (copy_file(self, SIXLIFT_BIN, 0755) != 0) {
        fprintf(stderr, "Failed to copy binary to %s\n", SIXLIFT_BIN);
        return 1;
    }
    printf("  - command installed at %s\n", SIXLIFT_BIN);

    const char *svc =
        "[Unit]\n"
        "Description=sixlift watchdog (self-heals IPv4 over IPv6)\n"
        "After=network-online.target\n"
        "Wants=network-online.target\n\n"
        "[Service]\n"
        "Type=oneshot\n"
        "ExecStart=" SIXLIFT_BIN " heal\n";
    const char *tmr =
        "[Unit]\n"
        "Description=Run sixlift watchdog every 2 minutes\n\n"
        "[Timer]\n"
        "OnBootSec=1min\n"
        "OnUnitActiveSec=2min\n"
        "Unit=sixlift-watchdog.service\n\n"
        "[Install]\n"
        "WantedBy=timers.target\n";

    if (write_file(SIXLIFT_SERVICE, svc) != 0 ||
        write_file(SIXLIFT_TIMER, tmr) != 0) {
        fprintf(stderr, "Failed to write systemd units.\n");
        return 1;
    }
    printf("  - systemd watchdog created\n");

    char *dr[] = {"systemctl", "daemon-reload", NULL};
    proc_run_quiet(dr);
    mkdir(SIXLIFT_STATEDIR, 0755);

    printf("Installed. Enable with:  sudo sixlift on\n");
    return 0;
}

int sl_uninstall(void)
{
    if (require_root("uninstall"))
        return 1;

    printf("Uninstalling sixlift...\n");

    char name[256], dev[64];
    if (get_active(name, sizeof(name), dev, sizeof(dev)) == 0) {
        nm_clear_dns(name);
        printf("  - DNS of '%s' reverted\n", name);
    }

    char *dis[] = {"systemctl", "disable", "--now",
                   "sixlift-watchdog.timer", NULL};
    proc_run_quiet(dis);
    unlink(SIXLIFT_SERVICE);
    unlink(SIXLIFT_TIMER);
    char *dr[] = {"systemctl", "daemon-reload", NULL};
    proc_run_quiet(dr);
    printf("  - systemd watchdog removed\n");

    unlink(SIXLIFT_BIN);
    unlink(SIXLIFT_STATE);
    rmdir(SIXLIFT_STATEDIR);
    unlink(SIXLIFT_LOG);

    printf("Fully uninstalled. Nothing left behind.\n");
    return 0;
}
