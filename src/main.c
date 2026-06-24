#include "sixlift/config.h"
#include "sixlift/service.h"

#include <stdio.h>
#include <string.h>

static void usage(void)
{
    printf(
        "sixlift %s — IPv4 over IPv6 (NAT64/DNS64) with a self-healing watchdog\n\n"
        "Usage:\n"
        "  sudo sixlift install     Install the command and the watchdog\n"
        "  sudo sixlift on          Enable NAT64 + watchdog (self-healing)\n"
        "  sudo sixlift off         Disable and revert everything\n"
        "  sixlift status           Show current state\n"
        "  sixlift test             Test whether IPv4-over-IPv6 works\n"
        "  sixlift log              Show the watchdog log\n"
        "  sudo sixlift uninstall   Revert and remove all installed files\n",
        SIXLIFT_VERSION);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage();
        return 0;
    }

    const char *cmd = argv[1];

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
