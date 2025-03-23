#include "utils.h"
#include <pthread.h>
#include "topics.h"
#include "mosquitto_mqtt.h"
#include "capture.h"
#include <libgen.h> //for basename()

#define MAX_CLIENTS 8

struct scanner_client {
    char id[MAX_ID_LEN];
    unsigned long last_msg;
};

struct manager_ctx {
    struct scanner_client clients[MAX_CLIENTS];
    size_t client_count;
} *ctx;

int get_client_idx(struct scanner_client *list, size_t n, char *id) {
    for(int i = 0; i < n; i ++) {
        if (!strcmp(list[i].id, id))
           return i;
    }
    return -1;
}

void handle_data_ap(char *topic)
{
    printf("%s() %s\n", __func__, topic);
}

void handle_data_pkt(char *topic)
{
    printf("%s() %s\n", __func__, topic);
}

int remove_client(int idx)
{
    for (int i = idx; i < ctx->client_count; i ++) {
        ctx->clients[i] = ctx->clients[i + 1];
    }
    ctx->client_count --;
}

void handle_client_state(char *id) {
    topic_t reg_ack = {0, 2};
    payload_t empty = {0};
    struct scanner_client client;
    int recv_id_len;
    int idx = get_client_idx(ctx->clients, ctx->client_count, id);

    if (idx >= 0) {
        printf("Unregister client %s\n", id);
        remove_client(idx);
        return;
    }

    if (ctx->client_count == MAX_CLIENTS) {
        printf("Max number of clients reached\n");
        return;
    }

    recv_id_len = strlen(id);

    if (!strlen) {
        printf("Empty ID, do nothing.\n");
        return;
    }

    strncpy(client.id, id, MAX_ID_LEN);
    ctx->clients[ctx->client_count++] = client;
    printf("Registered new client: %s\n", client.id);

    sprintf(reg_ack.name, "%s/%s/%s", TOPIC_CMD_BASE, client.id, SCANNER_REG_ACK);
    mqtt_publish_topic(reg_ack, empty);
}

void msg_recv_cb(const char *topic, void *data, unsigned int len)
{
    int tlen = strlen(topic);

    if (mqtt_is_sub_match(MANAGER_SUB_DATA_PKT, topic)) {
        //cmd is last part of the topic, so we can do a cheeky trick here 
        //since topic group is separated by /
        handle_data_pkt(dirname(topic));
    } else if (mqtt_is_sub_match(MANAGER_SUB_DATA_AP_LIST, topic)) {
        //first check failed so we must have got a cmd for our ID
        handle_data_ap(dirname(topic));
    } else if (mqtt_is_sub_match(MANAGER_SUB_CMD_REGISTER, topic) ||
        mqtt_is_sub_match(MANAGER_SUB_CMD_STOP, topic)) {
        //first check failed so we must have got a cmd for our ID
        handle_client_state(basename(topic));
    } 
}

//ugliest thing ive ever seen
void prepare_topics(char *client_id, topic_t *topics) 
{
    topic_t data_pkt = {MANAGER_SUB_DATA_PKT, 1}; //any command that is adressed to all 
    topic_t data_ap = {MANAGER_SUB_DATA_AP_LIST, 1}; //cmd directed to this specific client
    topic_t cmd_reg = {MANAGER_SUB_CMD_REGISTER, 2};
    topic_t cmd_stop = {MANAGER_SUB_CMD_STOP, 1};

    topics[0] = data_pkt;
    topics[1] = data_ap;
    topics[2] = cmd_reg;
    topics[3] = cmd_stop;
}

void *mqtt_thread_func(void *arg)
{
    topic_t *sub_topics = (topic_t *)arg;
    topic_t will = {CMD_ALL_STOP, 1};
    mqtt_setup(sub_topics, will, &msg_recv_cb);
    mqtt_run();
    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{ 
    ctx = malloc(sizeof(struct manager_ctx));

    pthread_t mqtt_thread;

    topic_t sub_topics[MQTT_MAX_TOPICS] = {0};
  
    prepare_topics(NULL, sub_topics);
    
    pthread_create(&mqtt_thread, NULL, &mqtt_thread_func, (void*)sub_topics);

    pthread_join(mqtt_thread, NULL);

    return EXIT_SUCCESS;
}

