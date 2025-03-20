
#include <linux/limits.h>

#define MQTT_CONFIG_FILE "client_mqtt.conf"
#define MAX_TOPIC_LEN 256
enum topic_id {
    TOPIC_APS,
    TOPIC_PACKETS,
    TOPIC_CMD,
    TOPIC_MAX
};

typedef struct mqtt_topic {
    char name[MAX_TOPIC_LEN];
    int qos;
} topic_t;

typedef struct mqtt_payload {
    u_int8_t type;
    u_int32_t len;
    void *data;
} payload_t;

#define CONN_RETRY_CNT 5
struct mosquitto_conf {
    char *host;
    int port;
    char *username;
    char *password;
    char cafile[PATH_MAX];
};

#define MQTT_CUSTOM_CB_COUNT 32
typedef void (*mqtt_cb)(void* data, u_int32_t len);
int mqtt_register_cb(mqtt_cb *func);
int mqtt_subscribe_topic(int topic_id);
int mqtt_publish_topic(int topic_id, payload_t *payload);
int mqtt_run();