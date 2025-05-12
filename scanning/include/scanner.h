#ifndef SCANNER_H
#define SCANNER_H

#include <pcap.h>
#include <pthread.h>
#include "utils.h"
#include "capture.h"
#include "mosquitto_mqtt.h"
#include <libgen.h> //for basename()
#include "topics.h"
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

struct scanner_client_ctx
{
    char *dev;
    char *mqtt_conf_path;
    int chanlist[14];
    int n_chans;
    char *client_id;
    topic_t sub_topics[MQTT_MAX_TOPICS];
    int registered;
    struct wifi_ap_info selected_ap;
};

#endif
