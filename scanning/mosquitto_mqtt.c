#include <sys/types.h>
#include <mosquitto.h>
#include <string.h>
#include <stdio.h>
#include "mosquitto_mqtt.h"
#include "utils.h"
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

static struct mqtt_ctx
{
    struct mosquitto *mosquitto;
    struct mosquitto_conf config;
    topic_t *sub_topics;
    mqtt_cb on_message;
    int cb_count;
    pthread_mutex_t lock;
} *ctx;

extern struct threads_shared shared;

int mqtt_is_sub_match(char *sub, char *topic)
{
    bool match_res;
    int ret;
    ret = mosquitto_topic_matches_sub(sub, topic, &match_res);
    return match_res;
}

int mqtt_subscribe_topic(topic_t topic)
{
    pthread_mutex_lock(&ctx->lock);
    int ret = mosquitto_subscribe(ctx->mosquitto, NULL, topic.name, topic.qos);

    if (ret != MOSQ_ERR_SUCCESS)
        fprintf(stderr, "Failed subscription: %s, %s\n", topic.name, mosquitto_strerror(ret));

    // printf("subscribe topic: %s qos: %d\n", topic.name, topic.qos);
    pthread_mutex_unlock(&ctx->lock);
    return ret;
}

int mqtt_publish_topic(topic_t topic, payload_t payload)
{
    pthread_mutex_lock(&ctx->lock);
    int message_id;

    // printf("publish topic: %s len: %d\n", topic.name, payload.len);
    int ret = mosquitto_publish(ctx->mosquitto, &message_id, topic.name,
                                payload.len, payload.data, topic.qos, false);

    if (ret)
        fprintf(stderr, "Failed publish: %s, %s\n", topic.name, mosquitto_strerror(ret));
    else
        ret = message_id;

    pthread_mutex_unlock(&ctx->lock);
    return ret;
}

void mqtt_cleanup()
{
    if (!ctx)
        return;
    if (ctx->mosquitto)
        mosquitto_destroy(ctx->mosquitto);
    pthread_mutex_destroy(&ctx->lock);
    mosquitto_lib_cleanup();
    free(ctx);
}

static int mqtt_read_config(char *path)
{
    FILE *fp;
    size_t len;
    ssize_t count;
    char *line = NULL, *key, *value;

    // for strtol
    char *end;
    long val;
    fp = fopen(path, "r");
    if (fp == NULL)
    {
        fprintf(stderr, "Can't open MQTT config file\n");
        return -1;
    }

    while ((count = getline(&line, &len, fp)) != -1)
    {
        if (line[count - 1] == '\n')
            line[count - 1] = '\0';

        key = strtok(line, "=");
        value = strtok(NULL, "=");

        if (!value || strlen(value) == 0)
            continue;
        wfs_debug("Key: '%s', Value: '%s'\n", key, value);

        if (strcmp(key, "HOST") == 0)
        {
            ctx->config.host = strdup(value);
            ctx->config.host[count - 1] = '\0';
        }
        else if (strcmp(key, "PORT") == 0)
        {
            ctx->config.port = (int)strtol(value, &end, 10);
        }
        else if (strcmp(key, "USERNAME") == 0)
        {
            ctx->config.username = strdup(value);
            ctx->config.username[count - 1] = '\0';
        }
        else if (strcmp(key, "PASSWORD") == 0)
        {
            ctx->config.password = strdup(value);
            ctx->config.password[count - 1] = '\0';
        }
        else
        {
            fprintf(stderr, "Unknown key in MQTT config: %s\n", key);
        }
    }

    free(line);

    fclose(fp);

    if (!ctx->config.host || !ctx->config.port)
    {
        fprintf(stderr, "Broker host or port not set, check MQTT config.\n");
        return -1;
    }

    return 0;
}

static void mqtt_on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
    if (!ctx->on_message)
        return;
    ctx->on_message(msg->topic, msg->payload, msg->payloadlen);
}

static void mqtt_on_connect(struct mosquitto *mosquitto, void *obj, int reason_code)
{
    int ret;
    if (reason_code != 0)
    {
        return;
    }
    for (int i = 0; i < MQTT_MAX_TOPICS; i++)
    {
        if (!strlen(ctx->sub_topics[i].name))
            break;
        mqtt_subscribe_topic(ctx->sub_topics[i]);
    }
}

static void mqtt_on_publish(struct mosquitto *mosquitto, void *obj, int message_id)
{
    // stub
}

static int mqtt_setup_login()
{
    int ret;

    // safe to not check user/pass
    if ((ret = mosquitto_username_pw_set(ctx->mosquitto, ctx->config.username, ctx->config.password)) != MOSQ_ERR_SUCCESS)
    {
        // syslog(LOG_ERR, "User settings error");
        fprintf(stderr,"MQTT user settings error (%d)\n", ret);
        return ret;
    }
    return MOSQ_ERR_SUCCESS;
}
// TODO mosquitto tls setup

const char *mqtt_get_user()
{
    // Won't be changing, reading only so no locks here
    return ctx->config.username;
}

int mqtt_set_will(topic_t will)
{
    int ret;
    if ((ret = mosquitto_will_set(ctx->mosquitto, will.name, 0, NULL, will.qos, false)))
    {
        fprintf(stderr, "MQTT will set error: %s\n", mosquitto_strerror(ret));
        return ret;
    }
    return MOSQ_ERR_SUCCESS;
}

int mqtt_set_sub_topics(topic_t *topics)
{
    ctx->sub_topics = topics;
}

int mqtt_setup(char *mqtt_conf_path, mqtt_cb on_msg_cb)
{
    int ret;
    if (ctx && ctx->mosquitto)
        return MOSQ_ERR_ALREADY_EXISTS;
    ctx = malloc(sizeof(struct mqtt_ctx));
    memset(ctx, 0, sizeof(struct mqtt_ctx));
    pthread_mutex_init(&ctx->lock, NULL);

    if ((ret = mosquitto_lib_init()) != MOSQ_ERR_SUCCESS)
    {
        fprintf(stderr, "Can't initialize mosquitto lib\n");
        return ret;
    }

    ctx->on_message = on_msg_cb;

    if (( ret = mqtt_read_config(mqtt_conf_path)))
        return MOSQ_ERR_INVAL;

    ctx->mosquitto = mosquitto_new(NULL, true, NULL);
    if ((ret = mqtt_setup_login()) != MOSQ_ERR_SUCCESS)
        return ret;

    mosquitto_connect_callback_set(ctx->mosquitto, mqtt_on_connect);
    mosquitto_message_callback_set(ctx->mosquitto, mqtt_on_message);
    mosquitto_publish_callback_set(ctx->mosquitto, mqtt_on_publish);

    printf("MQTT setup done\n");
    return MOSQ_ERR_SUCCESS;
}

static int mqtt_try_reconnect(struct mosquitto *mosquitto, int retry_count)
{
    int ret;
    for (int i = 0; i < retry_count; i++)
    {
        if (mosquitto_reconnect(mosquitto) == MOSQ_ERR_SUCCESS)
            return MOSQ_ERR_SUCCESS;
        sleep(5);
    }

    return MOSQ_ERR_CONN_LOST;
}

static void mqtt_loop()
{
    int ret;

    while (1)
    {
        pthread_mutex_lock(&shared.lock);
        if (shared.stop)
        {
            printf("Stop MQTT thread\n");
            pthread_mutex_unlock(&shared.lock);
            break;
        }
        pthread_mutex_unlock(&shared.lock);

        ret = mosquitto_loop(ctx->mosquitto, -1, 1);
        if (ret == MOSQ_ERR_SUCCESS)
            continue;
        switch (ret)
        {
        case MOSQ_ERR_CONN_LOST:
            fprintf(stderr, "Lost connection to broker\n");
            continue;
        case MOSQ_ERR_NO_CONN:
            if (mqtt_try_reconnect(ctx->mosquitto, CONN_RETRY_CNT) != MOSQ_ERR_SUCCESS)
            {
                fprintf(stderr, "Couldn't reconnect to broker after multiple attemps, exiting\n");
            } else continue;
            break;
        default:
            fprintf(stderr, "Error: %s", mosquitto_strerror(ret));
            break;
        }

        // i can just write returns in each of these cases, or just put the whole switch in this
        // conditional, but eh
        if (ret != MOSQ_ERR_SUCCESS) {
            shared.stop = 1;
            return;
        }
    }
}

int mqtt_run()
{
    int ret;
    if (!ctx || !ctx->mosquitto)
        return MOSQ_ERR_INVAL;

    printf("Start MQTT\n");
    if ((ret = mosquitto_connect(ctx->mosquitto, ctx->config.host, ctx->config.port, 10)) != MOSQ_ERR_SUCCESS)
    {
        fprintf(stderr, "Can't connect to broker\n");
        return ret;
    }

    mqtt_loop();

    mosquitto_disconnect(ctx->mosquitto);
    return MOSQ_ERR_SUCCESS;
}