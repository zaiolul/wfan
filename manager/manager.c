#include "manager.h"

static struct manager_ctx *ctx;

struct threads_shared shared = {0};

//Welfords algorithm, single pass variance
void update_client_scan_stats(struct scanner_client *client, struct cap_pkt_info *list, size_t count)
{   float var = 0, avg = 0, prev_avg = 0, prev_var = 0;
    int n = 0;
    int signal;

    var = client->stats.variance;
    avg = client->stats.average;

    for(int i = 0; i < count; i ++) {
        n ++;
        signal = list[i].radio.antenna_signal;
        avg = prev_avg + (signal - prev_avg) / n;
        var = prev_var + (signal - avg) * (signal - prev_avg);
        prev_avg = avg;
        prev_var = var;
    }

    client->stats.average = avg;
    client->stats.variance = var / (n - 1);
    client->stats.done = 1;
}

int create_results_dir()
{
    int ret;
    ret = mkdir(OUTPUT_DIR, 0775);
    if (!ret)
        return 0;
    switch (errno) {
        case EEXIST:
            printf("Results dir already exists\n");
            return 0;
        break;
        case ENOENT:
            printf("Can't create dir\n");
        break;
        default:
            printf("some other error\n");
        break;
    }
    return ret;
}

int client_close_resfile(struct scanner_client *client)
{
    if (!client->result_file)
        return 0;
    
    fclose(client->result_file);
    client->result_file = NULL;
}

int client_open_resfile(struct scanner_client *client)
{
    FILE *fs;
    char filename[PATH_MAX];
    if (client->result_file) 
        return 0;

    snprintf(filename, PATH_MAX, "%s/%s_%ld%s", OUTPUT_DIR, client->id, ctx->cap_start_time, RES_FILE_EXT);
    client->result_file = fopen(filename, "a+");

    if (!client->result_file) {
        printf("failed to open result file: %d", errno);
        return 1;
    }

    return 0;
}

int client_res_printf(struct scanner_client *client, const char *fmt, ...)
{
    va_list args;
    int ret;
    if (!client->result_file)
        return -1;

    va_start(args, fmt);
    ret = vfprintf(client->result_file, fmt, args);
    va_end( args );

    return ret;
}

void next_state(state_t state) {
    ctx->prev_state = ctx->state;
    ctx->state = state;
}

void set_state_idle() 
{
    pthread_mutex_lock(&shared.lock);
        printf("%s()\n");
    next_state(IDLE);
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
    next_state(SEARCHING);
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
            ptr[idx].count ++; 
        } else {
            memcpy(&ptr[i].ap, ap_list + i, sizeof(struct wifi_ap_info));
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

void write_pkt_data(struct scanner_client *client, struct cap_pkt_info *list, size_t count)
{
    struct radio_info *r;
    if (!client->result_file)
        return; //TODO: gracefully handle and maybe unregister client, should not probably reach this far if file not created tbh
    

    for (int i = 0; i < count; i ++) {
        r = &list[i].radio;
        if (client_res_printf(client, "%d;%f\n",
            r->antenna_signal, r->antenna_signal - client->stats.average) < 0)
            printf("failed to write client result\n");
    }
    fflush(client->result_file);
}

struct wifi_ap_info *select_shared_ap(int idx) 
{
    if (idx < 0 || idx >= ctx->n_cmn_ap)
        return NULL;
    printf("SELECTED AP: %s\n", ctx->common_aps[idx].ssid);
    return &ctx->common_aps[idx];
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

    if (!msg) {
        free(entries);
        return;
    }
    
    switch (msg->type) {
        case AP_LIST:
            client->ap_count = msg->count;
            memcpy(client->ap_list, msg->ap_list, sizeof(struct wifi_ap_info) * msg->count);
            ctx->finished_scans++;

            if (ctx->finished_scans == ctx->ready_clients) {
                printf("All ready clients done scanning\n");
                n_entries = find_common_aps(&entries);
                save_common_aps(entries, n_entries);
                next_state(SELECT_AP);
            }
            break;

        case PKT_LIST:
            if (!client->stats.done) {
                update_client_scan_stats(client, msg->pkt_list, msg->count);
                if (client_open_resfile(client)) {
                    printf("Can't open client results file\n");
                    break;
                }
                client_res_printf(client, "%s;"MAC_FMT"\n", ctx->selected_ap.ssid, MAC_BYTES(ctx->selected_ap.bssid)); //ssid;mac
                client_res_printf(client, "%d;%f;%f\n", msg->pkt_list[0].radio.channel_freq,
                    client->stats.average, client->stats.variance); //channel, listen average, listen variance
                break;
            }
          
         
            write_pkt_data(client, msg->pkt_list, msg->count);
            break;
    }
    free(entries);

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
        client_close_resfile(&ctx->clients[idx]);
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

//test
// void timer_learn(union sigval)
// {
//    pthread_mutex_lock(&shared.lock);
   
//    pthread_mutex_unlock(&shared.lock);
// }

int main(int argc, char *argv[])
{   
    int ret;
    pthread_t mqtt_thread, input_thread;
    topic_t sub_topics[MQTT_MAX_TOPICS] = {0};
    topic_t will = {CMD_ALL_STOP, 1};
    topic_t select_ap = {MANAGER_PUB_CMD_SELECT_AP, 1};
    struct wifi_ap_info *ap_ptr;
    payload_t ap_payload = {NULL, sizeof(struct wifi_ap_info)};

    // set_timer(3, timer_learn);

    ctx = malloc(sizeof(struct manager_ctx));
    ctx->state = IDLE;
    ctx->prev_state = ctx->state;
    ctx->require_input = 1;

    pthread_mutex_init(&shared.lock, NULL);
    prepare_topics(NULL, sub_topics);

    if ((ret = mqtt_setup(sub_topics, will,&msg_recv_cb)))
            goto mqtt_err;

    if (create_results_dir())
        return EXIT_FAILURE;

    pthread_create(&mqtt_thread, NULL, &mqtt_thread_func, NULL);

    int opt;

    while (1) {
        pthread_mutex_lock(&shared.lock);
        if (shared.stop) {
            pthread_mutex_unlock(&shared.lock);
            break;
        }
        switch (ctx->state) {
            case SELECT_AP:
                //this whole thing is poc, update it later
                while (1) {
                    printf("select ap to scan:");
                    print_ap_list(ctx->common_aps, ctx->n_cmn_ap);
                    scanf(" %d", &opt);
                    ap_ptr = select_shared_ap(opt);
                        if (!ap_ptr)
                            continue;
                    break;
                }
                memcpy(&ctx->selected_ap, ap_ptr, sizeof(struct wifi_ap_info)); 
                printf("send ap: %s\n", ap_ptr->ssid);
                ap_payload.data = (void*)ap_ptr;
                mqtt_publish_topic(select_ap, ap_payload);
                time(&ctx->cap_start_time);
                next_state(IDLE);

                break;
            case IDLE:
            case SEARCHING:
            case CAPTURING:
                break;
        }
        if (ctx->state != IDLE) { 
            pthread_mutex_unlock(&shared.lock);
            continue;
        }
        pthread_mutex_unlock(&shared.lock);

        scanf(" %d", &opt);
        switch (opt) {
            case 1:
                set_state_idle();
                break;
            case 2:
                set_state_searching();
                break;
            case 0: 
                pthread_mutex_lock(&shared.lock);
                shared.stop = 1;
                pthread_mutex_unlock(&shared.lock);
                break;
            default:
                break;
        }
        fflush(stdin);
    }

    pthread_join(mqtt_thread, NULL);

mqtt_err:
    mqtt_cleanup();
    free(ctx);
    return ret;
}

