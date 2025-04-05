
#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "capture_types.h"
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#ifdef DEBUG
#define wfs_debug(fmt, ...) fprintf(stderr, fmt, __VA_ARGS__)
#else
#define wfs_debug(fmt, ...) \
    do { } while (0);
#endif

#define MAX_ID_LEN 32

#define MS_TO_NS(ms) (ms * 1000000)

enum mgmt_frame_subtypes {
    FRAME_SUBTYPE_ASSOC_REQ = 0,
    FRAME_SUBTYPE_ASSOC_RESP = 1,
    FRAME_SUBTYPE_REASSOC_REQ = 2,
    FRAME_SUBTYPE_REASSOC_RESP = 3,
    FRAME_SUBTYPE_PROBE_REQ = 4,
    FRAME_SUBTYPE_PROBE_RESP = 5,
    FRAME_SUBTYPE_BEACON = 8,
    FRAME_SUBTYPE_DISASSOC = 10,
    FRAME_SUBTYPE_AUTH = 11,   
    FRAME_SUBTYPE_DEAUTH = 12,
    FRAME_SUBTYPE_ACTION = 13,
};

enum ctrl_frame_subtypes {
    FRAME_SUBTYPE_BLOCK_ACK = 9,
    FRAME_SUBTYPE_RTS =11,
};

enum frame_types {
    FRAME_TYPE_MGMT = 0,
    FRAME_TYPE_CTRL = 1,
    FRAME_TYPE_DATA = 2,
};

#define FRAME_CTRL_VER(ctrl) (*ctrl & 0x03)
#define FRAME_CTRL_TYPE(ctrl) ((*ctrl >> 2) & 0x03)
#define FRAME_CTRL_SUBTYPE(ctrl) ((*ctrl >> 4) & 0xF)

#define MAC_FMT "%02x:%02x:%02x:%02x:%02x:%02x"

#define MAC_BYTES(mac) mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]

char *wfs_mgmt_frame_to_str(enum mgmt_frame_subtypes subtype);
char *wfs_frame_type_to_str(enum frame_types type);

void wfs_print_mac(u_int8_t *mac);
char *get_client_id(char *iface);
int is_valid_mac(unsigned char* mac);
void print_ap_list(struct wifi_ap_info *list, size_t n);

#define ARR_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

timer_t set_timer(int sec, long nsec, void (*cb)(union sigval), int one_shot);
long long time_millis();
long long time_elapsed_ms(long long start);
int msleep(long msec);
int bssid_equal(unsigned char *a, unsigned char *b);
#endif
