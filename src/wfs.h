#ifndef WFS_H
#define WFS_H

#include <pcap.h>
#include <pthread.h>
#include "utils.h"
#include "capture.h"

#define WFS_VERSION "0.0"
extern char *error_msg;

struct wfs_ctx{
    char *dev;
    pcap_t *handle;
    int chanlist[14];
    int n_chans;
};

void parse_args(int argc, char *argv[], struct wfs_ctx *ctx);
struct wfs_ctx *wfs_alloc_ctx();
void wfs_free_ctx(struct wfs_ctx *ctx);
int wfs_start_capture(pcap_t *ctx);

void open_cmd_sock();
#endif
