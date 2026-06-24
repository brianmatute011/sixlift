#include "sixlift/service.h"

#include "sixlift/config.h"
#include "sixlift/platform.h"
#include "sixlift/probe.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- helpers ---------------------------------------------------------- */

static void wlog(const char *fmt, ...)
{
    FILE *f = fopen(plat_log_path(), "a");
    if (!f)
        return;
    time_t t = time(NULL);
    struct tm tm_;
    char ts[32] = "";
#if defined(_WIN32)
    struct tm *tp = localtime(&t);
    if (tp) tm_ = *tp;
    if (strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_) == 0) ts[0] = '\0';
#else
    if (localtime_r(&t, &tm_))
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_);
#endif
    fprintf(f, "%s ", ts);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static int require_admin(const char *action)
{
    if (!plat_is_admin()) {
        fprintf(stderr,
                "This needs administrator privileges to run '%s'.\n", action);
        return -1;
    }
    return 0;
}

static int state_set(int enabled)
{
    if (enabled) {
        plat_mkdir(plat_state_dir());
        FILE *f = fopen(plat_state_path(), "w");
        if (!f)
            return -1;
        fclose(f);
        return 0;
    }
    remove(plat_state_path()); /* C-standard, portable */
    return 0;
}

static int state_enabled(void)
{
    return plat_exists(plat_state_path());
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
    if (require_admin("on"))
        return 1;

    char conn[256], dev[128];
    if (plat_active_connection(conn, sizeof(conn), dev, sizeof(dev)) != 0) {
        fprintf(stderr, "No active network connection found.\n");
        return 1;
    }

    printf("Enabling NAT64/DNS64 on: %s\n", conn);
    state_set(1);
    plat_set_dns64(conn, dev, SIXLIFT_DNS64, SIXLIFT_DNS64_COUNT);

    if (plat_watchdog_installed()) {
        if (plat_watchdog_set(1) == 0)
            printf("Watchdog enabled (self-healing every 2 min).\n");
    } else {
        printf("Watchdog not installed (optional): run 'sixlift install'.\n");
    }

    printf("DNS64 applied.\n\n");
    return sl_test();
}

int sl_off(void)
{
    if (require_admin("off"))
        return 1;

    char conn[256], dev[128];
    int have = plat_active_connection(conn, sizeof(conn), dev, sizeof(dev)) == 0;

    printf("Disabling NAT64/DNS64 and reverting to normal...\n");
    state_set(0);

    if (plat_watchdog_installed() && plat_watchdog_set(0) == 0)
        printf("  - watchdog stopped\n");

    if (have) {
        plat_clear_dns(conn, dev);
        printf("  - DNS of '%s' restored\n", conn);
    }
    printf("Done. Everything back to normal.\n");
    return 0;
}

int sl_status(void)
{
    char conn[256] = "?", dev[128] = "?";
    plat_active_connection(conn, sizeof(conn), dev, sizeof(dev));

    printf("sixlift %s (%s) — status\n", SIXLIFT_VERSION, plat_name());
    printf("Active connection : %s (interface %s)\n", conn, dev);
    printf("Enabled           : %s\n", state_enabled() ? "YES" : "no");
    printf("Command installed : %s\n", plat_exists(plat_bin_path()) ? "yes" : "no");
    if (plat_watchdog_installed())
        printf("Watchdog          : %s\n",
               plat_watchdog_active() ? "installed & active"
                                      : "installed (stopped)");
    else
        printf("Watchdog          : not installed\n");
    printf("\n");
    return sl_test();
}

int sl_heal(void)
{
    if (!state_enabled())  /* sixlift is off — do nothing */
        return 0;
    if (probe_nat64())     /* healthy */
        return 0;

    wlog("NAT64 not responding — attempting repair");

    int del = plat_remove_ipv6_blackhole();
    if (del > 0)
        wlog("  - removed %d IPv6 blackhole route(s)", del);

    char conn[256], dev[128];
    int have = plat_active_connection(conn, sizeof(conn), dev, sizeof(dev)) == 0;

    if (!probe_ipv6() && have) {
        wlog("  - base IPv6 down; reconnecting '%s'", conn);
        plat_reconnect(conn);
    }
    if (have)
        plat_set_dns64(conn, dev, SIXLIFT_DNS64, SIXLIFT_DNS64_COUNT);

    if (probe_nat64())
        wlog("  OK repaired");
    else
        wlog("  FAIL still down (ISP IPv6 down, or both NAT64 providers down?)");
    return 0;
}

int sl_log(void)
{
    FILE *f = fopen(plat_log_path(), "r");
    if (!f) {
        printf("No log yet (%s).\n", plat_log_path());
        return 0;
    }
    char line[1024];
    while (fgets(line, sizeof(line), f))
        fputs(line, stdout);
    fclose(f);
    return 0;
}

int sl_install(void)
{
    if (require_admin("install"))
        return 1;

    printf("Installing sixlift %s (%s)...\n", SIXLIFT_VERSION, plat_name());

    char self[4096];
    if (plat_self_path(self, sizeof(self)) != 0) {
        fprintf(stderr, "Could not determine own path.\n");
        return 1;
    }
    if (plat_copy_exec(self, plat_bin_path()) != 0) {
        fprintf(stderr, "Failed to install binary to %s\n", plat_bin_path());
        return 1;
    }
    printf("  - command installed at %s\n", plat_bin_path());

    if (plat_watchdog_install(plat_bin_path()) != 0) {
        fprintf(stderr, "Failed to install watchdog.\n");
        return 1;
    }
    printf("  - watchdog created\n");

    plat_mkdir(plat_state_dir());
    printf("Installed. Enable with:  sixlift on\n");
    return 0;
}

int sl_uninstall(void)
{
    if (require_admin("uninstall"))
        return 1;

    printf("Uninstalling sixlift...\n");

    char conn[256], dev[128];
    if (plat_active_connection(conn, sizeof(conn), dev, sizeof(dev)) == 0) {
        plat_clear_dns(conn, dev);
        printf("  - DNS of '%s' reverted\n", conn);
    }

    plat_watchdog_set(0);
    plat_watchdog_remove();
    printf("  - watchdog removed\n");

    remove(plat_bin_path());
    remove(plat_state_path());
    remove(plat_log_path());
    printf("Fully uninstalled. Nothing left behind.\n");
    return 0;
}
