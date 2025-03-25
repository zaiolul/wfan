#include "manager.h"

static struct manager_ctx *ctx;

struct threads_shared shared = {0};

void set_state_idle() 
{
    pthread_mutex_lock(&shared.lock);
        printf("%s()\n");
    ctx->state = IDLE;
    topic_t start_idle = {MANAGER_PUB_CMD_STOP, 1};
    payload_t empty = {0};
    ctx->ready_clients = 0;
    for (int i = 0; i < ctx->client_count; i ++) {
        ctx->clients[i].ready = 0;
    }

    mqtt_publish_topic(start_idle, empty);
    pthread_mutex_unlock(&shared.lock);
}

void set_state_searching()
{
    pthread_mutex_lock(&shared.lock);
    ctx->state = SEARCHING;
    printf("%s()\n");
    topic_t start_scan = {MANAGER_PUB_CMD_SCAN, 1};
    payload_t empty = {0};
    ctx->ready_clients = 0;
    ctx->finished_scans = 0;

    for (int i = 0; i < ctx->client_count; i ++) {
        ctx->clients[i].ready = 1;
        ctx->ready_clients ++;
    }

    mqtt_publish_topic(start_scan, empty);

    pthread_mutex_unlock(&shared.lock);
}

void state_capturing()
{
    pthread_mutex_lock(&shared.lock);
    pthread_mutex_unlock(&shared.lock);
}

void state_select_ap()
{

}

int get_client_idx(struct scanner_client *list, size_t n, char *id) {
    for(int i = 0; i < n; i ++) {
        if (!strcmp(list[i].id, id))
           return i;
    }
    return -1;
}

int contains_ap_entry(struct ap_entry **entries, size_t n_entries, struct wifi_ap_info ap)
{
    struct ap_entry *ptr = *entries;
    for (int i = 0; i < n_entries; i++) {
        if (memcmp(ap.bssid, ptr[i].ap.bssid, 6) == 0) {
            return i;
        }
    }
    return -1;
}

void find_ap_counters(struct ap_entry **entries, size_t *n_entries, struct wifi_ap_info *ap_list, size_t count)
{
    int idx;
    struct ap_entry *ptr = *entries;

    for(int i = 0; i < count; i ++) {
        idx = contains_ap_entry(entries, *n_entries, ap_list[i]);
        if (idx >= 0) {
            printf("AP %s ("MAC_FMT")already exists\n",  ap_list[i].ssid, MAC_BYTES(ap_list[i].bssid));
            ptr[idx].count ++; 
        } else {
            printf("Found new AP %s ("MAC_FMT")\n",  ap_list[i].ssid, MAC_BYTES(ap_list[i].bssid));
            memcpy(ptr[i].ap.bssid, ap_list[i].bssid, 6);
            ptr[*n_entries].count = 1;
            (*n_entries) ++;
        }
    }
}

size_t find_common_aps(struct ap_entry **entries)
{
    size_t n_entries = 0;

    for(int i = 0; i < ctx->client_count; i ++) {
        if (!ctx->clients[i].ready)
            continue;
        find_ap_counters(entries, &n_entries, ctx->clients[i].ap_list, ctx->clients[i].ap_count);
    }
    return n_entries;
}

void save_common_aps(struct ap_entry *entries, size_t n_entries)
{
    ctx->n_cmn_ap = 0;
    memset(ctx->common_aps, 0, sizeof(struct wifi_ap_info) * AP_MAX);
    for (int i = 0; i < n_entries; i ++) {
        if (entries[i].count != ctx->ready_clients)
            continue;
        memcpy(&ctx->common_aps[ctx->n_cmn_ap ++], &entries[i].ap, sizeof(struct wifi_ap_info));
    }
}

void handle_data(char *topic, void *data, unsigned int len)
{
    int idx;
    char *id, *topic_dup;
    cap_msg_t *msg;
    struct scanner_client *client;
    struct ap_entry *entries;
    size_t n_entries;

    entries = malloc(sizeof(struct ap_entry) * AP_MAX);
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

            client->ap_count = msg->count;
            memcpy(client->ap_list, msg->ap_list, sizeof(struct wifi_ap_info) * msg->count);

            ctx->finished_scans++;
            if (ctx->finished_scans == ctx->ready_clients) {
                printf("All ready clients done scanning\n");
                n_entries = find_common_aps(&entries);
                save_common_aps(entries, n_entries);
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
    pthread_mutex_lock(&shared.lock);
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
    pthread_mutex_unlock(&shared.lock);
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
    mqtt_run();
    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{   
    int ret;
    pthread_t mqtt_thread, input_thread;
    topic_t sub_topics[MQTT_MAX_TOPICS] = {0};
    topic_t will = {CMD_ALL_STOP, 1};

    ctx = malloc(sizeof(struct manager_ctx));
    ctx->state = IDLE;
    ctx->prev_state = ctx->state;
    ctx->require_input = 1;

    pthread_mutex_init(&shared.lock, NULL);
    prepare_topics(NULL, sub_topics);

    if ((ret = mqtt_setup(sub_topics, will,&msg_recv_cb)))
            goto mqtt_err;
            
    pthread_create(&mqtt_thread, NULL, &mqtt_thread_func, NULL);

    char opt[1];

    while (1) {
        pthread_mutex_lock(&shared.lock);
        if (shared.stop) {
            pthread_mutex_unlock(&shared.lock);
            break;
        }
        pthread_mutex_unlock(&shared.lock);

        printf("1: stop 2: search 0: exit\n");
        read(0, opt, 1);
        switch (opt[0]) {
            case '1':
                set_state_idle();
                break;
            case '2':
                set_state_searching();
                break;
            case '0': 
                pthread_mutex_lock(&shared.lock);
                shared.stop = 1;
                pthread_mutex_unlock(&shared.lock);
                break;
            default:
                break;
        }
    }

    pthread_join(mqtt_thread, NULL);

mqtt_err:
    mqtt_cleanup();
    free(ctx);
    return ret;
}

