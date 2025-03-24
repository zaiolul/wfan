#include "scanner.h"

#define TO_STR_VAL(val) #val
#define TO_STR(val) TO_STR_VAL(val)

static struct scanner_client_ctx *ctx;
struct mqtt_arg {
    topic_t *sub_topics;
    char *client_id;
};

void check_stop_client() 
{
    payload_t empty = {0};
    topic_t reg_topic = {0, 2};

    if(!ctx->registered)
        return;

    sprintf(reg_topic.name,  "%s/%s", SCANNER_PUB_CMD_REGISTER, ctx->client_id);
    mqtt_publish_topic(reg_topic, empty);
    ctx->registered = 0;
}

void sig_handler(int signal) 
{   
    pthread_mutex_lock(&ctx->lock);
    switch (signal) {
        case SIGINT:
            ctx->stop = 1;
            check_stop_client();
            cap_override_state(STATE_END);
            break;
        default:
            break;
    }
    pthread_mutex_unlock(&ctx->lock);
}

void parse_chanlist(char *chanlist) {
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

void parse_args(int argc, char *argv[])
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
                parse_chanlist(optarg);
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

void msg_send_cb(cap_msg_t *msg)
{
    pthread_mutex_lock(&ctx->lock);
    payload_t payload;
    topic_t topic;

    topic.qos = 1;
    payload.data = (void*)msg;
    payload.len =  sizeof(cap_msg_t);

    sprintf(topic.name, "%s/%s", SCANNER_PUB_DATA, ctx->client_id);
    
    mqtt_publish_topic(topic, payload);
    pthread_mutex_unlock(&ctx->lock);
}

void handle_cmd_all(char *cmd)
{
    if (!strcmp(cmd, CMD_STOP)) {
        printf("GOT CMD STOP\n");
        ctx->registered = 0;
        cap_override_state(STATE_END);
    } else if (!strcmp(cmd, CMD_SCAN)) {
        printf("DO SCAN\n");
        cap_override_state(STATE_AP_SEARCH_START);
    }
}

void handle_cmd_id(char *cmd, void *data, unsigned int len)
{   
    if (!strcmp(cmd, SCANNER_REG_ACK)) {
        ctx->registered = 1;
    }
}

void msg_recv_cb(const char *topic, void *data, unsigned int len)
{
    pthread_mutex_lock(&ctx->lock);
    int tlen = strlen(topic);

    if (mqtt_is_sub_match(SCANNER_SUB_CMD_ALL, topic)) {
        //cmd is last part of the topic, so we can do a cheeky trick here 
        //since topic group is separated by /
        handle_cmd_all(basename(topic));
    } else if (mqtt_is_sub_match(SCANNER_SUB_CMD_ID, topic)) {
        //first check failed so we must have got a cmd for our ID
        handle_cmd_id(basename(topic), data, len);
    }
    pthread_mutex_unlock(&ctx->lock);
}

//ugliest thing ive ever seen
void prepare_topics(char *client_id, topic_t *topics) 
{
    topic_t cmd_all = {SCANNER_SUB_CMD_ALL, 1}; //any command that is adressed to all 
    topic_t cmd_id = {0, 1}; //cmd directed to this specific client
    snprintf(cmd_id.name, MAX_TOPIC_LEN, "%s/%s/+", TOPIC_CMD_BASE, client_id);// cant do it differently
    printf("topic id:%s\n", cmd_id.name);
    topics[0] = cmd_all;
    topics[1] = cmd_id;
}

int try_register() 
{
    //empty message with topic
    payload_t empty = {0};
    topic_t reg_topic = {0, 2};
    sprintf(reg_topic.name, "%s/%s", SCANNER_PUB_CMD_REGISTER, ctx->client_id);
    while (1) {
        pthread_mutex_lock(&ctx->lock);
        if (ctx->stop) {
            pthread_mutex_unlock(&ctx->lock);
            break;
        }
        pthread_mutex_unlock(&ctx->lock);

        mqtt_publish_topic(reg_topic, empty);
        sleep(2); // give some time to message to go through

        if (ctx->registered) {
            printf("register ok\n");
            return 0;
        }
        sleep(5);
        printf("try next\n");
    }
    printf("Failed to receive reg ack, exit\n");
    return -1;
}

void *mqtt_thread_func(void *arg)
{
    struct mqtt_arg *targ = (struct mqtt_arg*)arg;

    topic_t will = {0, 1};
    sprintf(will.name, "%s/%s", SCANNER_PUB_CMD_STOP, targ->client_id);
    mqtt_setup(targ->sub_topics, will,&msg_recv_cb);
    mqtt_run();
    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{ 
    pthread_t mqtt_thread;
    struct mqtt_arg targ;
    struct sigaction act;
    act.sa_handler = sig_handler;
    sigaction(SIGINT, &act, NULL);

    wfs_debug("--START--\n", NULL);

    signal(SIGTERM, sig_handler);

    pcap_init(PCAP_CHAR_ENC_UTF_8, NULL);

    ctx = malloc(sizeof(struct scanner_client_ctx));
    pthread_mutex_init(&ctx->lock, NULL);
    ctx->registered = 0;
    ctx->stop = 0;

    if (ctx == NULL) {
        fprintf(stderr, "Failed to allocate memory for context\n");
        return EXIT_FAILURE;
    }
    
    parse_args(argc, argv);

    ctx->client_id = get_client_id(ctx->dev);
    prepare_topics(ctx->client_id, ctx->sub_topics);
    
    targ.client_id = ctx->client_id;
    targ.sub_topics = ctx->sub_topics;
    pthread_create(&mqtt_thread, NULL, &mqtt_thread_func, (void*)&targ);
    
    sleep(1); // wait a bit for mqtt to init first

    while (1) {
        if (try_register())
            break;
        cap_start_capture(ctx->dev, &msg_send_cb);
    }
         
    pthread_cancel(mqtt_thread);
    pthread_join(mqtt_thread, NULL);

    free(ctx);
    return EXIT_SUCCESS;

}