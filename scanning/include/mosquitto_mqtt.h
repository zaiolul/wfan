#ifndef MOSQUITTO_MQTT_H
#define MOSQUITTO_MQTT_H

#include <linux/limits.h>

#define MQTT_CONFIG_FILE "client_mqtt.conf"
#define MAX_TOPIC_LEN 256

#define MQTT_MAX_TOPICS 16
typedef struct mqtt_topic {
    char name[MAX_TOPIC_LEN];
    int qos;
} topic_t;

typedef struct mqtt_payload {
    void *data;
    u_int32_t len;
} payload_t;

#define CONN_RETRY_CNT 5
struct mosquitto_conf {
    char *host;
    int port;
    char *username;
    char *password;
};

struct threads_shared {
    pthread_mutex_t lock;
    int stop;
};

typedef void (*mqtt_cb)(const char *topic, void* data, u_int32_t len);
int mqtt_register_cb(mqtt_cb *func);
int mqtt_subscribe_topic(topic_t topic);
int mqtt_publish_topic(topic_t topic, payload_t payload);
int mqtt_setup(char *mqtt_conf_path, mqtt_cb on_msg_cb);
int mqtt_is_sub_match(char* sub, char *topic);
int mqtt_set_sub_topics(topic_t *topics);
int mqtt_set_will(topic_t will);
const char *mqtt_get_user();
void mqtt_cleanup();
int mqtt_run();

#endif