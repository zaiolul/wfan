#ifndef WFS_H
#define WFS_H

#include <pcap.h>
#include <pthread.h>
#include "utils.h"
#include "capture.h"
#include "mosquitto_mqtt.h"
#include <libgen.h> //for basename()
#include "topics.h"
#include <unistd.h>


struct wfs_ctx{
    char *dev;
    int chanlist[14];
    int n_chans;
    char *client_id;
    topic_t sub_topics[MQTT_MAX_TOPICS];
};

#endif
