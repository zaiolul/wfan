#include <sys/types.h>
#include <mosquitto.h>
#include <string.h>
#include <stdio.h>
#include "mosquitto_mqtt.h"
#include "utils.h"
#include <stdlib.h>
#include <unistd.h>

static struct mqtt_ctx {
    struct mosquitto *mosquitto;
    struct mosquitto_conf config;
    int cb_count;
} *ctx;

static const topic_t topics[] = {
    [TOPIC_APS] = {"ap", 1},
    [TOPIC_PACKETS] = {"packets", 2},
    [TOPIC_CMD] = {"cmd", 1},
};

int mqtt_subscribe_topic(int topic_id)
{
    topic_t topic;

    if (topic_id < 0 && topic_id >= TOPIC_MAX)
        return -1;

    topic = topics[topic_id];

    int ret = mosquitto_subscribe(ctx->mosquitto, NULL, topic.name, topic.qos);

    if (ret != MOSQ_ERR_SUCCESS)
        printf("Failed subscription: %s, %s\n", topic.name,  mosquitto_strerror(ret));
    return ret;
}

int mqtt_publish_topic(int topic_id, payload_t *payload)
{
    topic_t topic;
    int message_id;

    if (topic_id < 0 && topic_id >= TOPIC_MAX)
        return -1;

    topic = topics[topic_id];
           
    int ret = mosquitto_publish(ctx->mosquitto, &message_id, topic.name, 
        payload->len, payload->data, topic.qos, false);

    if (ret == MOSQ_ERR_SUCCESS)
        return message_id;

    printf("Failed publish: %s, %s\n", topic.name,  mosquitto_strerror(ret));
    return ret;
}

static void mqtt_cleanup() {
    if (!ctx->mosquitto)
        return;

    mosquitto_destroy(ctx->mosquitto);
    mosquitto_lib_cleanup();
    free(ctx);
}

static int mqtt_read_config()
{
    FILE *fp = fopen(MQTT_CONFIG_FILE, "r");
    size_t len;
    ssize_t count;
    char *line = NULL, *key, *value;

    //for strtol
    char *end;
    long val;
    
    if(fp == NULL) {
        printf("Can't open mqtt config file\n");
        return -1;
    }

    while((count = getline(&line, &len, fp)) != -1) {
        if (line[count - 1] == '\n')
            line[count - 1] = '\0';

        key = strtok(line, "=");
        value = strtok(NULL, "=");

        if (!value || strlen(value) == 0)
            continue;
        wfs_debug("Key: '%s', Value: '%s'\n", key, value);
        
        if(strcmp(key, "HOST") == 0) {
            ctx->config.host = strdup(value);
            ctx->config.host[count - 1] = '\0';
        } else if(strcmp(key, "PORT") == 0) {
            ctx->config.port = (int)strtol(value, &end, 10);
        } else if(strcmp(key, "USERNAME") == 0) {
            ctx->config.username = strdup(value);
            ctx->config.username[count - 1] = '\0';
        } else if(strcmp(key, "PASSWORD") == 0) {
            ctx->config.password = strdup(value);
            ctx->config.password[count - 1] = '\0';
        } else {
            printf("Unknown key in config file: %s\n", key);
        }
    }

    wfs_debug("Host: %s, port: %d user: %s pass: %s\n", conf->host, conf->port, conf->username, conf->password);
    free(line);

    fclose(fp);

    if (!ctx->config.host || !ctx->config.port) {
        printf(" Broker host or port not set, check config.\n");
        return -1;
    }
    
    return 0;
}

static void mqtt_on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
    
}

static void mqtt_on_connect(struct mosquitto *mosquitto, void *obj, int reason_code)
{
    printf("mqtt: %s\n", mosquitto_connack_string(reason_code));
    
    int ret;
	if(reason_code != 0){
        return;
	}

    for(int i = 0; i < TOPIC_MAX; i ++) {
        ret = mqtt_subscribe_topic(i);
        if (ret != MOSQ_ERR_SUCCESS)
            printf("Failed to subscribe topic: %s\n", topics[i].name);
    }
}

static void mqtt_on_publish(struct mosquitto *mosquitto, void *obj, int message_id)
{
    //stub
}

static int mqtt_setup_login()
{
    int ret;
    
    //safe to not check user/pass
    if ((ret = mosquitto_username_pw_set(ctx->mosquitto, ctx->config.username, ctx->config.password)) != MOSQ_ERR_SUCCESS) {
        // syslog(LOG_ERR, "User settings error");
        printf("User settings error (%d)\n", ret);
        return ret;
    }
    return MOSQ_ERR_SUCCESS;
}
//TODO mosquitto tls setup

static int mqtt_setup()
{
    int ret;

    if (ctx->mosquitto)
        return MOSQ_ERR_ALREADY_EXISTS;

    ret = mqtt_read_config();

    if (ret) 
        return MOSQ_ERR_INVAL;

    if((ret = mosquitto_lib_init()) != MOSQ_ERR_SUCCESS){
        printf("Can't initialize mosquitto lib\n");
        return ret;
    }

    ctx->mosquitto = mosquitto_new(NULL, true, NULL);
    if ((ret = mqtt_setup_login()) != MOSQ_ERR_SUCCESS)
        return ret;
    
    mosquitto_connect_callback_set(ctx->mosquitto, mqtt_on_connect);
    mosquitto_message_callback_set(ctx->mosquitto, mqtt_on_message);
    mosquitto_publish_callback_set(ctx->mosquitto, mqtt_on_publish);

    if ((ret = mosquitto_connect(ctx->mosquitto, ctx->config.host, ctx->config.port, 60)) != MOSQ_ERR_SUCCESS) {
        printf("Can't connect to broker\n");
        return ret;
    }

    return MOSQ_ERR_SUCCESS;
}

static int mqtt_try_reconnect(struct mosquitto *mosquitto, int retry_count)
{
    int ret;
    for (int i = 0; i < retry_count; i ++) {
        if(mosquitto_reconnect(mosquitto) == MOSQ_ERR_SUCCESS)
            return MOSQ_ERR_SUCCESS;
        sleep(1);
    }

    return MOSQ_ERR_CONN_LOST;
}

static void mqtt_loop()
{
    int ret;

    while (1) {
        ret = mosquitto_loop(ctx->mosquitto, -1, 1);
        if (ret == MOSQ_ERR_SUCCESS)
            continue;
        switch (ret){
            case MOSQ_ERR_CONN_LOST:
                printf("Lost connection to broker\n");
                break;
            case MOSQ_ERR_NO_CONN:
                if (mqtt_try_reconnect(ctx->mosquitto, CONN_RETRY_CNT) != MOSQ_ERR_SUCCESS) {                 
                    printf("Couldn't reconnect to broker after multiple attemps, exiting\n");
                }    
                break;
            default:
                printf("Unhandled error: %s", mosquitto_strerror(ret));
                break;
        }

        //i can just write returns in each of these cases, or just put the whole switch in this
        //conditional, but eh
        if (ret != MOSQ_ERR_SUCCESS)
            return;
    }
}

int mqtt_run()
{
    int ret;
    if (!ctx->mosquitto)
        return MOSQ_ERR_INVAL;
        
    mqtt_loop();
    mosquitto_disconnect(ctx->mosquitto);
    mqtt_cleanup();
    return MOSQ_ERR_SUCCESS;
}