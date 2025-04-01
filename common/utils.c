
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include "utils.h"


char *wfs_mgmt_frame_to_str(enum frame_subtypes subtype) {
    switch (subtype) {
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

char *wfs_frame_type_to_str(enum frame_types type) {
    switch (type) {
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
    printf(MAC_FMT"\n", MAC_BYTES(mac));
}

char *set_dev_mac(char *iface, unsigned char *buf)
{
    int fd;
	struct ifreq ifr;
	
	fd = socket(AF_INET, SOCK_DGRAM, 0);

	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name , iface , IFNAMSIZ-1);

	ioctl(fd, SIOCGIFHWADDR, &ifr);

	close(fd);
	
	memcpy(buf, ifr.ifr_hwaddr.sa_data, 6);
}

int is_valid_mac(unsigned char* mac)
{
    unsigned char empty_mac[] = {0, 0, 0, 0, 0, 0};
    if (memcmp(mac, empty_mac, 6) == 0)
        return 0;
    return 1;
}

void print_ap_list(struct wifi_ap_info *list, size_t n) 
{
    for (int i = 0; i < n; i ++) {
        printf("%d: %s "MAC_FMT"\n", 
        i,
        strlen(list[i].ssid) > 0 ? (char*)list[i].ssid : "<hidden>",
        MAC_BYTES(list[i].bssid));
    }
}

char *get_client_id(char *iface)
{
    unsigned char *id = malloc(MAX_ID_LEN);
    char hostname[MAX_ID_LEN - 15]; //mac + underscore
    
    gethostname(hostname, sizeof(hostname));
    set_dev_mac(iface, id);
    sprintf(id, "%s_"MAC_FMT, hostname, MAC_BYTES(id));
    return id;
}

int set_timer(int sec, long nsec, void (*cb)(union sigval))
{
    struct sigevent sev;
    timer_t timerid;
    struct itimerspec its = {0};

    // Configure the timer to call timer_handler
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = cb;
    sev.sigev_value.sival_ptr = NULL;
    sev.sigev_notify_attributes = NULL;

    if (timer_create(CLOCK_MONOTONIC, &sev, &timerid) != 0) {
        fprintf(stderr, "timer_create failed\n");
        return 1;
    }

    its.it_value.tv_sec = sec;
    its.it_value.tc_nsec = 0;

    if (timer_settime(timerid, 0, &its, NULL) != 0) {
        fprintf(stderr, "timer_settime failed\n");
        return 1;
    }
}