#ifndef SIXLIFT_SERVICE_H
#define SIXLIFT_SERVICE_H

/* Subcommands. Each returns a process exit code (0 = success). */
int sl_on(void);        /* enable NAT64/DNS64 + start watchdog       */
int sl_off(void);       /* disable and revert everything             */
int sl_status(void);    /* print state and run a connectivity test   */
int sl_test(void);      /* test whether IPv4-over-IPv6 works          */
int sl_log(void);       /* show the watchdog log                     */
int sl_heal(void);      /* watchdog tick (run by systemd as root)    */
int sl_install(void);   /* install the binary + systemd watchdog     */
int sl_uninstall(void); /* revert and remove all installed files     */

#endif /* SIXLIFT_SERVICE_H */
