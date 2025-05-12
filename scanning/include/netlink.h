#ifndef _NETLINK_H
#define _NETLINK_H
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <linux/nl80211.h>
#include <errno.h>
#include <net/if.h>
#include "utils.h"

struct nl80211_data
{
    struct nl_sock *sock;
    int id;
    int ifindex;
};

int netlink_init(struct nl80211_data *nl, char *iface);
int netlink_deinit(struct nl80211_data *nl);
int netlink_switch_chan(struct nl80211_data *nl, int chan);
#endif