
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include "utils.h"

char *wfs_mgmt_frame_to_str(enum mgmt_frame_subtypes subtype)
{
    switch (subtype)
    {
    case FRAME_SUBTYPE_ASSOC_REQ:
        return "Association Request";
    case FRAME_SUBTYPE_ASSOC_RESP:
        return "Association Response";
    case FRAME_SUBTYPE_REASSOC_REQ:
        return "Reassociation Request";
    case FRAME_SUBTYPE_REASSOC_RESP:
        return "Reassociation Response";
    case FRAME_SUBTYPE_PROBE_REQ:
        return "Probe Request";
    case FRAME_SUBTYPE_PROBE_RESP:
        return "Probe Response";
    case FRAME_SUBTYPE_BEACON:
        return "Beacon";
    case FRAME_SUBTYPE_DISASSOC:
        return "Disassociation";
    case FRAME_SUBTYPE_AUTH:
        return "Authentication";
    case FRAME_SUBTYPE_DEAUTH:
        return "Deauthentication";
    case FRAME_SUBTYPE_ACTION:
        return "Action";
    default:
        return "Unknown";
    }
}

char *wfs_frame_type_to_str(enum frame_types type)
{
    switch (type)
    {
    case FRAME_TYPE_MGMT:
        return "Management";
    case FRAME_TYPE_CTRL:
        return "Control";
    case FRAME_TYPE_DATA:
        return "Data";
    default:
        return "Unknown";
    }
}

void wfs_print_mac(u_int8_t *mac)
{
    printf(MAC_FMT "\n", MAC_BYTES(mac));
}

char *set_dev_mac(char *iface, unsigned char *buf)
{
    int fd;
    struct ifreq ifr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);

    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);

    ioctl(fd, SIOCGIFHWADDR, &ifr);

    close(fd);

    memcpy(buf, ifr.ifr_hwaddr.sa_data, 6);
}

int is_valid_mac(unsigned char *mac)
{
    unsigned char empty_mac[] = {0, 0, 0, 0, 0, 0};
    if (memcmp(mac, empty_mac, 6) == 0)
        return 0;
    return 1;
}

void print_ap_list(struct wifi_ap_info *list, size_t n)
{
    for (int i = 0; i < n; i++)
    {
        printf("%d: %s " MAC_FMT " %d\n",
               i,
               strlen(list[i].ssid) > 0 ? (char *)list[i].ssid : "<hidden>",
               MAC_BYTES(list[i].bssid), list[i].channel);
    }
}

char *get_client_id(char *iface)
{
    unsigned char *id = malloc(MAX_ID_LEN);
    char hostname[MAX_ID_LEN - 15]; // mac + underscore

    gethostname(hostname, sizeof(hostname));
    set_dev_mac(iface, id);
    sprintf(id, "%s_" MAC_FMT, hostname, MAC_BYTES(id));
    return id;
}

timer_t set_timer(int sec, long nsec, void (*cb)(union sigval), void *cb_data, int one_shot)
{
    struct sigevent sev;
    timer_t timerid;
    struct itimerspec its = {0};

    // Configure the timer to call timer_handler
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = cb;
    sev.sigev_value.sival_ptr = cb_data;
    sev.sigev_notify_attributes = NULL;

    if (timer_create(CLOCK_REALTIME, &sev, &timerid) != 0)
    {
        fprintf(stderr, "timer_create failed\n");
        return NULL;
    }

    its.it_value.tv_sec = sec;
    its.it_value.tv_nsec = nsec;

    if (!one_shot)
    {
        its.it_interval.tv_sec = sec;
        its.it_interval.tv_nsec = nsec;
    }

    if (timer_settime(timerid, 0, &its, NULL) != 0)
    {
        fprintf(stderr, "timer_settime failed\n");
        return NULL;
    }
    return timerid;
}

long long time_millis()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (ts.tv_sec * 1000LL) + (ts.tv_nsec / 1000000);
}

long long time_elapsed_ms(long long start)
{
    long long now = time_millis();
    return now - start;
}

int msleep(long msec)
{
    struct timespec req = {0};
    time_t sec = msec / 1000;
    msec -= sec * 1000;
    req.tv_sec = sec;
    req.tv_nsec = msec * 1000000L;

    return nanosleep(&req, NULL);
}

int bssid_equal(unsigned char *a, unsigned char *b)
{
    return memcmp(a, b, 6) == 0;
}

// ugly, yes, but works
int bssid_str_to_val(char *str, unsigned char *bssid)
{
    char part[3];
    if (strlen(str) != 17) // 2 chars + 1 sep * 6
        return -1;

    for (int i = 0; i < 6; i++)
    {
        memcpy(part, &str[i * 3], 2);
        part[2] = '\0';

        bssid[i] = (unsigned char)strtol(part, NULL, 16);
    }
    return 0;
}