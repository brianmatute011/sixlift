#include "sixlift/service.h"

#include "sixlift/config.h"
#include "sixlift/log.h"
#include "sixlift/platform.h"
#include "sixlift/probe.h"

#include <stdio.h>
#include <stdlib.h>

/* ---- helpers ---------------------------------------------------------- */

static int require_admin(const char *action)
{
    if (!plat_is_admin()) {
        LOG_ERRORF("'%s' requires administrator privileges; refusing", action);
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
        if (!f) {
            LOG_ERRORF("could not write state marker '%s'", plat_state_path());
            return -1;
        }
        fclose(f);
        LOG_DEBUGF("state marker created at '%s'", plat_state_path());
        return 0;
    }
    remove(plat_state_path());
    LOG_DEBUGF("state marker removed");
    return 0;
}

static int state_enabled(void)
{
    return plat_exists(plat_state_path());
}

/* ---- subcommands ------------------------------------------------------ */

int sl_test(void)
{
    LOG_DEBUGF("running connectivity test");
    printf("Testing IPv4-only host (ipv4.google.com) over IPv6...\n");

    int nat64 = probe_nat64();
    LOG_INFOF("connectivity test: nat64=%s", nat64 ? "ok" : "fail");

    if (nat64) {
        printf("  OK: IPv4 is reaching the internet over IPv6.\n");
        return 0;
    }
    printf("  FAIL: the NAT64 path is not working.\n");
    int ipv6 = probe_ipv6();
    LOG_WARNF("connectivity test failed: nat64=fail ipv6=%s",
              ipv6 ? "ok" : "fail");
    if (!ipv6)
        printf("  Also: base IPv6 is currently down on this machine.\n");
    return 1;
}

int sl_on(void)
{
    LOG_INFOF("subcommand 'on' invoked");
    if (require_admin("on"))
        return 1;

    char conn[256], dev[128];
    if (plat_active_connection(conn, sizeof(conn), dev, sizeof(dev)) != 0) {
        LOG_ERRORF("no active network connection found");
        fprintf(stderr, "No active network connection found.\n");
        return 1;
    }
    LOG_INFOF("active connection '%s' on interface '%s'", conn, dev);

    printf("Enabling NAT64/DNS64 on: %s\n", conn);
    state_set(1);

    LOG_INFOF("applying %d DNS64 server(s)", SIXLIFT_DNS64_COUNT);
    plat_set_dns64(conn, dev, SIXLIFT_DNS64, SIXLIFT_DNS64_COUNT);

    if (plat_watchdog_installed()) {
        if (plat_watchdog_set(1) == 0) {
            LOG_INFOF("watchdog enabled");
            printf("Watchdog enabled (self-healing every 2 min).\n");
        } else {
            LOG_WARNF("failed to enable watchdog");
        }
    } else {
        LOG_DEBUGF("watchdog not installed; skipping");
        printf("Watchdog not installed (optional): run 'sixlift install'.\n");
    }

    printf("DNS64 applied.\n\n");
    return sl_test();
}

int sl_off(void)
{
    LOG_INFOF("subcommand 'off' invoked");
    if (require_admin("off"))
        return 1;

    char conn[256], dev[128];
    int have = plat_active_connection(conn, sizeof(conn), dev, sizeof(dev)) == 0;

    printf("Disabling NAT64/DNS64 and reverting to normal...\n");
    state_set(0);

    if (plat_watchdog_installed() && plat_watchdog_set(0) == 0) {
        LOG_INFOF("watchdog stopped");
        printf("  - watchdog stopped\n");
    }
    if (have) {
        LOG_INFOF("restoring DNS on '%s'", conn);
        plat_clear_dns(conn, dev);
        printf("  - DNS of '%s' restored\n", conn);
    } else {
        LOG_WARNF("no active connection; DNS not reverted");
    }
    printf("Done. Everything back to normal.\n");
    return 0;
}

int sl_status(void)
{
    char conn[256] = "?", dev[128] = "?";
    plat_active_connection(conn, sizeof(conn), dev, sizeof(dev));

    int enabled = state_enabled();
    int installed = plat_exists(plat_bin_path());
    int wd = plat_watchdog_installed();
    int wd_active = wd && plat_watchdog_active();
    LOG_DEBUGF("status: conn='%s' enabled=%d installed=%d watchdog=%d active=%d",
               conn, enabled, installed, wd, wd_active);

    printf("sixlift %s (%s) - status\n", SIXLIFT_VERSION, plat_name());
    printf("Active connection : %s (interface %s)\n", conn, dev);
    printf("Enabled           : %s\n", enabled ? "YES" : "no");
    printf("Command installed : %s\n", installed ? "yes" : "no");
    if (wd)
        printf("Watchdog          : %s\n",
               wd_active ? "installed & active" : "installed (stopped)");
    else
        printf("Watchdog          : not installed\n");
    printf("\n");
    return sl_test();
}

int sl_heal(void)
{
    if (!state_enabled()) {
        LOG_DEBUGF("heal: sixlift disabled, nothing to do");
        return 0;
    }
    if (probe_nat64()) {
        LOG_DEBUGF("heal: path healthy, nothing to do");
        return 0;
    }

    LOG_WARNF("heal: NAT64 path is down, attempting repair");

    int del = plat_remove_ipv6_blackhole();
    if (del > 0)
        LOG_INFOF("heal: removed %d IPv6 blackhole route(s)", del);

    char conn[256], dev[128];
    int have = plat_active_connection(conn, sizeof(conn), dev, sizeof(dev)) == 0;

    if (!probe_ipv6() && have) {
        LOG_WARNF("heal: base IPv6 down, reconnecting '%s'", conn);
        plat_reconnect(conn);
    }
    if (have) {
        LOG_INFOF("heal: re-asserting DNS64 on '%s'", conn);
        plat_set_dns64(conn, dev, SIXLIFT_DNS64, SIXLIFT_DNS64_COUNT);
    }

    if (probe_nat64()) {
        LOG_INFOF("heal: repair succeeded");
    } else {
        LOG_ERRORF("heal: repair failed; path still down "
                   "(ISP IPv6 outage or all NAT64 providers unreachable)");
    }
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
    LOG_INFOF("subcommand 'install' invoked");
    if (require_admin("install"))
        return 1;

    printf("Installing sixlift %s (%s)...\n", SIXLIFT_VERSION, plat_name());

    char self[4096];
    if (plat_self_path(self, sizeof(self)) != 0) {
        LOG_ERRORF("could not determine own executable path");
        fprintf(stderr, "Could not determine own path.\n");
        return 1;
    }
    LOG_DEBUGF("installing binary: '%s' -> '%s'", self, plat_bin_path());
    if (plat_copy_exec(self, plat_bin_path()) != 0) {
        LOG_ERRORF("failed to install binary to '%s'", plat_bin_path());
        fprintf(stderr, "Failed to install binary to %s\n", plat_bin_path());
        return 1;
    }
    printf("  - command installed at %s\n", plat_bin_path());

    if (plat_watchdog_install(plat_bin_path()) != 0) {
        LOG_ERRORF("failed to install watchdog");
        fprintf(stderr, "Failed to install watchdog.\n");
        return 1;
    }
    LOG_INFOF("watchdog installed");
    printf("  - watchdog created\n");

    plat_mkdir(plat_state_dir());
    printf("Installed. Enable with:  sixlift on\n");
    return 0;
}

int sl_uninstall(void)
{
    LOG_INFOF("subcommand 'uninstall' invoked");
    if (require_admin("uninstall"))
        return 1;

    printf("Uninstalling sixlift...\n");

    char conn[256], dev[128];
    if (plat_active_connection(conn, sizeof(conn), dev, sizeof(dev)) == 0) {
        LOG_INFOF("reverting DNS on '%s'", conn);
        plat_clear_dns(conn, dev);
        printf("  - DNS of '%s' reverted\n", conn);
    }

    plat_watchdog_set(0);
    plat_watchdog_remove();
    LOG_INFOF("watchdog removed");
    printf("  - watchdog removed\n");

    remove(plat_bin_path());
    remove(plat_state_path());
    remove(plat_log_path());
    printf("Fully uninstalled. Nothing left behind.\n");
    return 0;
}
