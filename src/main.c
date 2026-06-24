#include "sixlift/config.h"
#include "sixlift/log.h"
#include "sixlift/platform.h"
#include "sixlift/service.h"

#include <stdio.h>
#include <string.h>

static void usage(void)
{
    printf(
        "sixlift %s - IPv4 over IPv6 (NAT64/DNS64) with a self-healing watchdog\n\n"
        "Usage:\n"
        "  sixlift [options] <command>\n\n"
        "Commands:\n"
        "  install     Install the command and the watchdog (admin)\n"
        "  on          Enable NAT64 + watchdog, self-healing (admin)\n"
        "  off         Disable and revert everything (admin)\n"
        "  status      Show current state\n"
        "  test        Test whether IPv4-over-IPv6 works\n"
        "  log         Show the watchdog log\n"
        "  uninstall   Revert and remove all installed files (admin)\n\n"
        "Options:\n"
        "  -v, --verbose   Print INFO logs to stderr\n"
        "  -vv             Print DEBUG logs to stderr\n"
        "  -q, --quiet     Print only ERROR logs to stderr\n\n"
        "Environment:\n"
        "  SIXLIFT_LOG_LEVEL   File-log threshold: trace|debug|info|warn|error|fatal\n",
        SIXLIFT_VERSION);
}

static int dispatch(const char *cmd)
{
    if (!strcmp(cmd, "on"))        return sl_on();
    if (!strcmp(cmd, "off"))       return sl_off();
    if (!strcmp(cmd, "status"))    return sl_status();
    if (!strcmp(cmd, "test"))      return sl_test();
    if (!strcmp(cmd, "log"))       return sl_log();
    if (!strcmp(cmd, "heal"))      return sl_heal();
    if (!strcmp(cmd, "install"))   return sl_install();
    if (!strcmp(cmd, "uninstall")) return sl_uninstall();
    if (!strcmp(cmd, "help") || !strcmp(cmd, "-h") || !strcmp(cmd, "--help")) {
        usage();
        return 0;
    }
    fprintf(stderr, "Unknown command: %s\n\n", cmd);
    usage();
    return 1;
}

int main(int argc, char **argv)
{
    const char *cmd = NULL;
    log_level_t console = SL_WARN;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-v") || !strcmp(a, "--verbose"))
            console = SL_INFO;
        else if (!strcmp(a, "-vv"))
            console = SL_DEBUG;
        else if (!strcmp(a, "-q") || !strcmp(a, "--quiet"))
            console = SL_ERROR;
        else if (!cmd)
            cmd = a; /* first non-flag token is the command */
    }

    /* Initialize logging. The file sink lives at the platform log path and is
     * only writable by an admin; non-privileged commands simply skip it.
     * On POSIX, records are also forwarded to syslog (the systemd journal). */
    log_enable_syslog(1);
    log_init(plat_log_path(), "sixlift");
    log_set_console_level(console);

    if (!cmd) {
        usage();
        log_shutdown();
        return 0;
    }

    LOG_DEBUGF("sixlift %s starting on %s; command='%s'",
               SIXLIFT_VERSION, plat_name(), cmd);
    int rc = dispatch(cmd);
    LOG_DEBUGF("command '%s' finished with code %d", cmd, rc);

    log_shutdown();
    return rc;
}
