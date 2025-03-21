#ifndef WFS_H
#define WFS_H

#include <pcap.h>
#include <pthread.h>
#include "utils.h"
#include "capture.h"
#include "mosquitto_mqtt.h"
#include <libgen.h> //for basename()
#include "topics.h"

#define WFS_VERSION "0.0"


struct wfs_ctx{
    char *dev;
    int chanlist[14];
    int n_chans;
    char *client_id;
    topic_t sub_topics[MQTT_MAX_TOPICS];
};

void parse_args(int argc, char *argv[], struct wfs_ctx *ctx);
struct wfs_ctx *wfs_alloc_ctx();
void wfs_free_ctx(struct wfs_ctx *ctx);

void open_cmd_sock();
#endif
