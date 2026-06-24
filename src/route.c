#include "sixlift/route.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#define NL_BUFLEN 8192
#define MAX_HITS  16

static int nl_open(void)
{
    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0)
        return -1;

    struct sockaddr_nl addr;
    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Dump IPv6 routes and collect the metric of every blackhole default route
 * that lives in the local table. Returns the number found (capped at max). */
static int find_blackholes(int fd, uint32_t *metrics, int max)
{
    struct {
        struct nlmsghdr nlh;
        struct rtmsg rtm;
    } req;

    memset(&req, 0, sizeof(req));
    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    req.nlh.nlmsg_type = RTM_GETROUTE;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nlh.nlmsg_seq = 1;
    req.rtm.rtm_family = AF_INET6;

    if (send(fd, &req, req.nlh.nlmsg_len, 0) < 0)
        return -1;

    char buf[NL_BUFLEN];
    int count = 0, done = 0;

    while (!done) {
        ssize_t len = recv(fd, buf, sizeof(buf), 0);
        if (len <= 0)
            break;

        for (struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
             NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {

            if (nlh->nlmsg_type == NLMSG_DONE || nlh->nlmsg_type == NLMSG_ERROR) {
                done = 1;
                break;
            }
            if (nlh->nlmsg_type != RTM_NEWROUTE)
                continue;

            struct rtmsg *rtm = NLMSG_DATA(nlh);
            if (rtm->rtm_family != AF_INET6 || rtm->rtm_dst_len != 0 ||
                rtm->rtm_type != RTN_BLACKHOLE)
                continue;

            uint32_t table = rtm->rtm_table;
            uint32_t metric = 0;
            int rtl = (int)RTM_PAYLOAD(nlh);

            for (struct rtattr *rta = RTM_RTA(rtm); RTA_OK(rta, rtl);
                 rta = RTA_NEXT(rta, rtl)) {
                if (rta->rta_type == RTA_TABLE)
                    memcpy(&table, RTA_DATA(rta), sizeof(table));
                else if (rta->rta_type == RTA_PRIORITY)
                    memcpy(&metric, RTA_DATA(rta), sizeof(metric));
            }

            if (table != RT_TABLE_LOCAL)
                continue;
            if (count < max)
                metrics[count] = metric;
            count++;
        }
    }
    return count;
}

/* Delete one blackhole default route from the local table, matching metric. */
static int del_blackhole(int fd, uint32_t metric, unsigned seq)
{
    struct {
        struct nlmsghdr nlh;
        struct rtmsg rtm;
        char attrs[64];
    } req;

    memset(&req, 0, sizeof(req));
    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    req.nlh.nlmsg_type = RTM_DELROUTE;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    req.nlh.nlmsg_seq = seq;
    req.rtm.rtm_family = AF_INET6;
    req.rtm.rtm_dst_len = 0;
    req.rtm.rtm_table = RT_TABLE_LOCAL;
    req.rtm.rtm_type = RTN_BLACKHOLE;
    req.rtm.rtm_scope = RT_SCOPE_NOWHERE;
    req.rtm.rtm_protocol = RTPROT_UNSPEC;

    if (metric) {
        struct rtattr *rta =
            (struct rtattr *)((char *)&req + NLMSG_ALIGN(req.nlh.nlmsg_len));
        rta->rta_type = RTA_PRIORITY;
        rta->rta_len = RTA_LENGTH(sizeof(metric));
        memcpy(RTA_DATA(rta), &metric, sizeof(metric));
        req.nlh.nlmsg_len = NLMSG_ALIGN(req.nlh.nlmsg_len) + rta->rta_len;
    }

    if (send(fd, &req, req.nlh.nlmsg_len, 0) < 0)
        return -1;

    char buf[NL_BUFLEN];
    ssize_t len = recv(fd, buf, sizeof(buf), 0);
    if (len < 0)
        return -1;

    for (struct nlmsghdr *nlh = (struct nlmsghdr *)buf; NLMSG_OK(nlh, len);
         nlh = NLMSG_NEXT(nlh, len)) {
        if (nlh->nlmsg_type == NLMSG_ERROR) {
            struct nlmsgerr *err = NLMSG_DATA(nlh);
            return err->error; /* 0 == success (pure ACK) */
        }
    }
    return 0;
}

int route_delete_ipv6_blackhole_local(void)
{
    int fd = nl_open();
    if (fd < 0)
        return -1;

    uint32_t metrics[MAX_HITS];
    int n = find_blackholes(fd, metrics, MAX_HITS);
    if (n <= 0) {
        close(fd);
        return n < 0 ? -1 : 0;
    }

    int deleted = 0;
    for (int i = 0; i < n && i < MAX_HITS; i++) {
        if (del_blackhole(fd, metrics[i], (unsigned)(100 + i)) == 0)
            deleted++;
    }

    close(fd);
    return deleted;
}
