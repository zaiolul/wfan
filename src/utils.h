
#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#ifdef DEBUG
#define wfs_debug(fmt, ...) fprintf(stderr, fmt, __VA_ARGS__)
#else
#define wfs_debug(fmt, ...) \
    do { } while (0);
#endif

#define MAX_ID_LEN 32

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

void wfs_print_mac(u_int8_t *mac);
char *get_client_id(char *iface);

#endif
