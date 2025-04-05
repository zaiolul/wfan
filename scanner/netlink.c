#include "netlink.h"

int netlink_init(struct nl80211_data *nl, char *iface)
{
    int ret;
    nl->sock = nl_socket_alloc();
    if (!nl->sock)
    {
        fprintf(stderr, "Failed to allocate netlink socket\n");
        return -ENOMEM;
    }

    if ((ret = genl_connect(nl->sock)))
    {
        fprintf(stderr, "Failed to connect to netlink\n");
        goto free;
    }

    nl->id = genl_ctrl_resolve(nl->sock, "nl80211");
    if (nl->id < 0)
    {
        fprintf(stderr, "Failed to resolve nl80211\n");
        ret = -ENOENT;
        goto free;
    }

    nl->ifindex = if_nametoindex(iface);
    if (!nl->ifindex)
    {
        ret = -ENOENT;
        goto free;
    }

    return 0;
free:
    nl_socket_free(nl->sock);
    return ret;
}

int netlink_deint(struct nl80211_data *nl)
{
    if (!nl || !nl->sock)
        return -EINVAL;

    nl_socket_free(nl->sock);
    return 0;
}

int netlink_switch_chan(struct nl80211_data *nl, int chan)
{
    int freq, ret;

    if (!nl || !nl->sock)
        return -EINVAL;

    if (chan >= 1 && chan <= 13)
    {
        freq = 2407 + (chan * 5);
    }
    else if (chan >= 36 && chan <= 161)
    {
        // TODO: handle 5ghz
    }
    else
    {
        return -ERANGE;
    }
    printf("Set channel %d\n", chan);
    struct nl_msg *msg = nlmsg_alloc();
    genlmsg_put(msg, 0, 0, nl->id, 0, 0, NL80211_CMD_SET_CHANNEL, 0);

    NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, nl->ifindex);
    NLA_PUT_U32(msg, NL80211_ATTR_WIPHY_FREQ, freq);

    ret = nl_send_auto(nl->sock, msg);

    nlmsg_free(msg);
    // msleep(10); // 10 ms wait
    return 0;

nla_put_failure:
    nlmsg_free(msg);
    fprintf(stderr, "%s() put failure\n", __func__);
    return -1;
}