#include "scanner.h"
#define TO_STR_VAL(val) #val
#define TO_STR(val) TO_STR_VAL(val)

void parse_chanlist(char *chanlist, struct wfs_ctx *ctx) {
    char *chanlist_copy = strdup(chanlist);
    printf("test");
    wfs_debug("Chanlist: %s\n", chanlist_copy);
    char *token = strtok(chanlist_copy, ",");
    int i = 0;
    while (token != NULL) {
        ctx->chanlist[i++] = atoi(token);
        token = strtok(NULL, ",");
    }
    ctx->n_chans = i;
    free(chanlist_copy);
}


void parse_args(int argc, char *argv[], struct wfs_ctx *ctx)
{
    char *prog_opts = "d:v:c:";
    int opt;

    while ((opt = getopt(argc, argv, prog_opts)) != -1) {
        switch (opt) {
            case 'd':
                ctx->dev = strdup(optarg);
                wfs_debug("Device: %s\n", ctx->dev);
                break;
            case 'v':
                break;
            case 'c':
                parse_chanlist(optarg, ctx);
                break;
            default:
                printf("Usage: %s [-d device] [-v]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    
    if (!ctx->dev) {
        printf("Usage: %s [-d device] [-v] -c CHANNEL1,CHANNEL2...\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    //kazkoks default tokiu atveju
    if(ctx->n_chans == 0) {
        ctx->chanlist[0] = 1;
        ctx->n_chans = 1;
    }

    wfs_debug("Channels: %d\n", ctx->n_chans);
}

struct wfs_ctx *wfs_alloc_ctx() {
    struct wfs_ctx *ctx = malloc(sizeof(struct wfs_ctx));
    memset(ctx, 0, sizeof(struct wfs_ctx));
    return ctx;
}

void wfs_free_ctx(struct wfs_ctx *ctx){
    free(ctx->dev);
    free(ctx);
}

void msg_send_cb(cap_msg_t msg)
{
    payload_t payload;
    topic_t topic;

    topic.qos = 1;

    payload.data = msg.data;
    payload.len = sizeof(cap_msg_t) - sizeof(void*);

    printf("%s() type: %d count %d\n", __func__, msg.type, msg.count);

    switch (msg.type) {
        case AP_LIST:
            payload.len += sizeof(struct wifi_ap_info) * msg.count;
            memcpy(topic.name, SCANNER_PUB_DATA_APLIST, MAX_TOPIC_LEN);
            break;
        case PKT_LIST:
            payload.len += sizeof(struct cap_pkt_info) * msg.count;
            memcpy(topic.name, SCANNER_PUB_DATA_PKT, MAX_TOPIC_LEN);
            break;
        default:
            return;
    }
    mqtt_publish_topic(topic, payload);
}

void handle_cmd_all(char *cmd)
{
}

void handle_cmd_id(char *cmd, void *data, unsigned int len)
{
    
}

void msg_recv_cb(const char *topic, void *data, unsigned int len)
{
    int tlen = strlen(topic);

    if (mqtt_is_sub_match(SCANNER_SUB_CMD_ALL, topic)) {
        //cmd is last part of the topic, so we can do a cheeky trick here 
        //since topic group is separated by /
        handle_cmd_all(basename(topic));
    } else if (mqtt_is_sub_match(SCANNER_SUB_CMD_ID, topic)) {
        //first check failed so we must have got a cmd for our ID
        handle_cmd_id(basename(topic), data, len);
    }
}

//ugliest thing ive ever seen
void prepare_topics(char *client_id, topic_t *topics) 
{
    topic_t cmd_all = {SCANNER_SUB_CMD_ALL, 1}; //any command that is adressed to all 
    topic_t cmd_id = {0, 1}; //cmd directed to this specific client
    snprintf(cmd_id.name, MAX_TOPIC_LEN, "%s/%s/+", TOPIC_CMD_BASE, client_id);// cant do it differently

    topics[0] = cmd_all;
    topics[1] = cmd_id;
}

void *mqtt_thread_func(void *arg)
{
    topic_t *sub_topics = (topic_t *)arg;
    mqtt_setup(sub_topics, &msg_recv_cb);
    mqtt_run();
    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{ 
    pcap_init(PCAP_CHAR_ENC_UTF_8, NULL);
    wfs_debug("--START--\n", NULL);
    struct wfs_ctx *ctx;
   
    pthread_t mqtt_thread;
    ctx = wfs_alloc_ctx();

    if (ctx == NULL) {
        fprintf(stderr, "Failed to allocate memory for context\n");
        return EXIT_FAILURE;
    }
    
    parse_args(argc, argv, ctx);

    ctx->client_id = get_client_id(ctx->dev);
    prepare_topics(ctx->client_id, ctx->sub_topics);
    
    pthread_create(&mqtt_thread, NULL, &mqtt_thread_func, (void*)ctx->sub_topics);

    cap_start_capture(ctx->dev, &msg_send_cb); // loop

    pthread_join(mqtt_thread, NULL);

    cap_stop_capture();
    wfs_free_ctx(ctx);
    return EXIT_SUCCESS;
}