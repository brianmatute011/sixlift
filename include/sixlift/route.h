#ifndef SIXLIFT_ROUTE_H
#define SIXLIFT_ROUTE_H

/* Find and delete any IPv6 "blackhole default" routes in the local table
 * (table 255). Such a route — typically planted by a VPN/privacy kill-switch —
 * is matched before the main table and silently drops all IPv6 traffic.
 *
 * Implemented natively over rtnetlink (RTM_GETROUTE dump + RTM_DELROUTE),
 * matching on the exact metric of each route found.
 *
 * Returns the number of routes deleted (>= 0), or -1 on a netlink error. */
int route_delete_ipv6_blackhole_local(void);

#endif /* SIXLIFT_ROUTE_H */
