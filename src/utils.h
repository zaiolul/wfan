
#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/bits.h>


#ifdef DEBUG
#define wfs_debug(fmt, ...) fprintf(stderr, fmt, __VA_ARGS__)
#else
#define wfs_debug(fmt, ...) \
    do { } while (0);
#endif

#define CAP_BUF_SIZE 8096


struct radiotap_data {
    u_int8_t flags;
    u_int8_t rate; 
    u_int16_t channel_freq;
    u_int16_t channel_flags;
    int8_t antenna_signal;
    u_int8_t antenna;
    u_int16_t rx_flags;
};

struct hdr_radiotap {
    u_int8_t version;
    u_int8_t padding;
    u_int16_t length;
    u_int32_t present_flags;
    struct radiotap_data data;
};

#define RADIOTAP_BAND_24(hdr) (hdr->data.channel_flags & (1 << 7)) 
#define RADIOTAP_BAND_5(hdr) (hdr->data.channel_flags & (1 << 8)) 

enum frame_subtypes {
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

enum frame_types {
    FRAME_TYPE_MGMT = 0,
    FRAME_TYPE_DATA = 1,
    FRAME_TYPE_CTRL = 2,
};

#define FRAME_CTRL_VER(ctrl) (*ctrl & 0x03)
#define FRAME_CTRL_TYPE(ctrl) ((*ctrl >> 2) & 0x03)
#define FRAME_CTRL_SUBTYPE(ctrl) ((*ctrl >> 4) & 0xF)

//TODO management frame types

void set_error_msg(const char *msg, ...);

char *wfs_mgmt_frame_to_str(enum frame_subtypes subtype);
char *wfs_frame_type_to_str(enum frame_types type);
#endif
