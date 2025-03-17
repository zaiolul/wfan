#include <mosquitto.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/limits.h>

#define MQTT_CONFIG_FILE "client_mqtt.conf"


struct mosquitto_conf {
    char *host;
    int port;
    char *username;
    char *password;
    char cafile[PATH_MAX];
};