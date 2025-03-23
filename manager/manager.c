#include "utils.h"
#include <pthread.h>
#include "topics.h"
#include "mosquitto_mqtt.h"
#include "capture.h"
#include <libgen.h> //for basename()
#include <unistd.h>
#define MAX_CLIENTS 8
typedef enum state {
    IDLE,
    SEARCHING,
    CAPTURING
} state_t;

struct scanner_client {
    char id[MAX_ID_LEN];
    unsigned long last_msg;
    struct wifi_ap_info ap_list[AP_MAX];
    size_t ap_count;
    int finished_scan;
    int ready;
};

struct ap_entry {
    struct wifi_ap_info ap;
    int count;
};

struct manager_ctx {
    struct scanner_client clients[MAX_CLIENTS];
    size_t client_count;
    size_t finished_scans;
    size_t ready_clients;
    state_t state;
    state_t prev_state;
    pthread_mutex_t lock;
} *ctx;

struct ap_entry ap_counters[AP_MAX] = {0};
int ap_counters_n = 0;

void set_state_idle() 
{
    pthread_mutex_lock(&ctx->lock);
    topic_t start_idle = {MANAGER_PUB_CMD_STOP, 1};
    payload_t empty = {0};
    ctx->ready_clients = 0;
    for (int i = 0; i < ctx->client_count; i ++) {
        ctx->clients[i].ready = 0;
    }

    mqtt_publish_topic(start_idle, empty);
    pthread_mutex_unlock(&ctx->lock);
}

void set_state_searching()
{
    pthread_mutex_lock(&ctx->lock);
    topic_t start_scan = {MANAGER_PUB_CMD_SCAN, 1};
    payload_t empty = {0};
    ctx->ready_clients = 0;
    ctx->finished_scans = 0;
    ap_counters_n = 0;
    for (int i = 0; i < ctx->client_count; i ++) {
        ctx->clients[i].ready = 1;
        ctx->ready_clients ++;
    }

    mqtt_publish_topic(start_scan, empty);

    pthread_mutex_unlock(&ctx->lock);
}

void set_state_capturing()
{
    pthread_mutex_lock(&ctx->lock);
    pthread_mutex_unlock(&ctx->lock);
}


int get_client_idx(struct scanner_client *list, size_t n, char *id) {
    for(int i = 0; i < n; i ++) {
        if (!strcmp(list[i].id, id))
           return i;
    }
    return -1;
}

int contains_ap_entry(struct wifi_ap_info ap)
{
    for (int i = 0; i < ap_counters_n; i++) {
        if (memcmp(ap.bssid, ap_counters[i].ap.bssid, 6) == 0) {
            return i;  // Return index if found
        }
    }
    return -1;
}

void find_ap_counters(struct wifi_ap_info *list, size_t count)
{
    int idx;
    for(int i = 0; i < count; i ++) {
        idx = contains_ap_entry(list[i]);
        if (idx >= 0) {
            printf("AP %s already exists\n",  list[i].ssid);
            ap_counters[idx].count ++; 
        } else {
            printf("Found new AP %s\n",  list[i].ssid);
            memcpy(ap_counters[ap_counters_n].ap.bssid, list[i].bssid, 6);
            ap_counters[ap_counters_n].count = 1;
            ap_counters_n ++;
            
        }
    }
}

void find_common_aps()
{
    for(int i = 0; i < ctx->client_count; i ++) {
        if (!ctx->clients[i].ready)
            continue;
        printf("Find AP counters for %s (total aps: %d)\n",
            ctx->clients[i].id,
            ctx->clients[i].ap_count);
        find_ap_counters(ctx->clients[i].ap_list, ctx->clients[i].ap_count);
    }
}

void select_ap()
{
    struct wifi_ap_info aps[AP_MAX] = {0};
    int ap_count = 0;

    for (int i = 0; i < ap_counters_n; i ++) {
        if (ap_counters[i].count != ctx->ready_clients)
            continue;
        memcpy(&aps[ap_count ++], &ap_counters[i].ap, sizeof(struct wifi_ap_info));
        printf("%s: common ap %s\n", __func__, ap_counters[i].ap.ssid);
    }

}

void handle_data(char *topic, void *data, unsigned int len)
{
    int idx;
    char *id, *topic_dup;
    cap_msg_t *msg;
    struct scanner_client *client;
    printf("%s() %s len %d\n", __func__, topic, len);
    id = basename(topic);

    idx = get_client_idx(ctx->clients, ctx->client_count, id);
    if (idx < 0)
        return;
    
    client = &ctx->clients[idx];
    msg = (cap_msg_t *)data;

    if (!msg)
        return;
    
    switch (msg->type) {
        case AP_LIST:
            memset(ap_counters, 0, sizeof(ap_counters));
            ap_counters_n = 0;

            client->ap_count = msg->count;
            memcpy(client->ap_list, msg->ap_list, sizeof(struct wifi_ap_info) * msg->count);

            ctx->finished_scans++;
            if (ctx->finished_scans == ctx->ready_clients) {
                printf("All ready clients done scanning\n");
                find_common_aps();
                select_ap();
            }

            break;
        case PKT_LIST:
            break;
    }

}

int remove_client(int idx)
{
    for (int i = idx; i < ctx->client_count; i ++) {
        ctx->clients[i] = ctx->clients[i + 1];
    }
    ctx->client_count --;
}

void handle_client_state(char *cmd) {
    topic_t reg_ack = {0, 2};
    payload_t empty = {0};
    char *cmd_dup_id = strdup(cmd);
    char *cmd_dup = strdup(cmd);

    char *id = basename(cmd_dup_id);
    char *c = dirname(cmd_dup);

    struct scanner_client client = {0};
    int recv_id_len;
    int idx = get_client_idx(ctx->clients, ctx->client_count, id);

    if (idx >= 0) {
        printf("Unregister client %s\n", id);
        remove_client(idx);
        goto done;
    }

    if (ctx->client_count == MAX_CLIENTS) {
        printf("Max number of clients reached\n");
        goto done;
    }

    recv_id_len = strlen(id);

    if (!strlen) {
        printf("Empty ID, do nothing.\n");
        goto done;
    }

    if (!strncmp(c,CMD_REGISTER , sizeof(CMD_REGISTER))) {
        strncpy(client.id, id, MAX_ID_LEN);
        ctx->clients[ctx->client_count++] = client;
        printf("Registered new client: %s\n", client.id);

        sprintf(reg_ack.name, "%s/%s/%s", TOPIC_CMD_BASE, client.id, SCANNER_REG_ACK);
        mqtt_publish_topic(reg_ack, empty);    
    }

done:
    free(cmd_dup);
    free(cmd_dup_id);
}

void msg_recv_cb(const char *topic, void *data, unsigned int len)
{
    pthread_mutex_lock(&ctx->lock);
    int tlen = strlen(topic);

    if (mqtt_is_sub_match(MANAGER_SUB_DATA, topic)) {
        //cmd is last part of the topic, so we can do a cheeky trick here 
        //since topic group is separated by /
        handle_data(topic, data, len);
    } else if (mqtt_is_sub_match(MANAGER_SUB_CMD_REGISTER, topic) ||
        mqtt_is_sub_match(MANAGER_SUB_CMD_STOP, topic)) {
        //first check failed so we must have got a cmd for our ID
        char *cmd = topic + strlen(TOPIC_CMD_BASE) + 1;
        handle_client_state(cmd);
    } 
    pthread_mutex_unlock(&ctx->lock);
}

//ugliest thing ive ever seen
void prepare_topics(char *client_id, topic_t *topics) 
{
    topic_t data = {MANAGER_SUB_DATA, 1};
    topic_t cmd_reg = {MANAGER_SUB_CMD_REGISTER, 2};
    topic_t cmd_stop = {MANAGER_SUB_CMD_STOP, 1};

    topics[0] = data;
    topics[1] = cmd_reg;
    topics[2] = cmd_stop;
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
    pthread_t mqtt_thread;
    topic_t sub_topics[MQTT_MAX_TOPICS] = {0};

    ctx = malloc(sizeof(struct manager_ctx));
    ctx->state = IDLE;
    ctx->prev_state = ctx->state;
    pthread_mutex_init(&ctx->lock, NULL);
    prepare_topics(NULL, sub_topics);
    
    pthread_create(&mqtt_thread, NULL, &mqtt_thread_func, (void*)sub_topics);
    int run = 1;
    char opt[1];
    while (run) {
        printf("1: stop 2: scan 3: capture 0: exit\n");
        read(0, opt, 1);
        switch (opt[0]) {
            case '1':
                ctx->state = IDLE;
                set_state_idle();
                break;
            case '2':
                ctx->state = SEARCHING;
                set_state_searching();
                break;
            case '3':
                ctx->state = CAPTURING;
                set_state_capturing();
                break;
            case '0': 
                run = 0;
                break;
            default:
                break;
        }
    }
    pthread_join(mqtt_thread, NULL);

    return EXIT_SUCCESS;
}

