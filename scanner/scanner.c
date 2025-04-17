#include "scanner.h"
#include "cJSON.h"

#define TO_STR_VAL(val) #val
#define TO_STR(val) TO_STR_VAL(val)

static struct scanner_client_ctx *ctx;
static struct capture_ctx *cap_ctx;

struct threads_shared shared = {0};

void sig_handler(int signal) 
{   
    switch (signal) {
        case SIGINT:
            shared.stop = 1;
            break;
        default:
            break;
    }
}

void parse_args(int argc, char *argv[])
{
    char *prog_opts = "d:v:p:";
    int opt;

    while ((opt = getopt(argc, argv, prog_opts)) != -1) {
        switch (opt) {
            case 'd':
                ctx->dev = strdup(optarg);
                wfs_debug("Device: %s\n", ctx->dev);
                break;
            case 'v':
                break;
            case 'p':
                ctx->mqtt_conf_path = strdup(optarg);
                break;
            default:
                printf("Usage: %s [-d device] [-v]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    
    if (!ctx->dev) {
        printf("Usage: %s -d device [-v] [-p MQTT_USER_CONF]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    if (!ctx->mqtt_conf_path) {
        ctx->mqtt_conf_path = MQTT_CONFIG_FILE;
    }
}

//send json formatted string
void msg_send_cb(char *msg)
{
    pthread_mutex_lock(&shared.lock);
    payload_t payload;
    topic_t topic;

    topic.qos = 1;
    payload.data = (void*)msg;
    payload.len =  strlen(msg);

    sprintf(topic.name, "%s/%s", SCANNER_PUB_DATA, ctx->client_id);
    mqtt_publish_topic(topic, payload);

    pthread_mutex_unlock(&shared.lock);
}

void handle_cmd_all(char *cmd, void *data, unsigned int len)
{
    cJSON *json = NULL;
    cJSON *arr = NULL;
    cJSON *obj = NULL;
    char bssid_str[32]; //big enough buffer
    int channel;
    struct wifi_ap_info ap;
    int chans[128];
    int chan_count = 0;

    if (!strcmp(cmd, CMD_STOP)) {
        printf("GOT CMD STOP\n");
        ctx->registered = 0;
        memset(&ctx->selected_ap, 0 , sizeof(struct wifi_ap_info));
        cap_override_state(STATE_END);
    } else if (!strcmp(cmd, CMD_SCAN)) {
        if (!ctx->registered)
            return;
        printf("DO SCAN\n");
        json = cJSON_Parse(data);
        arr = cJSON_GetObjectItem(json, "channels");

        cJSON_ArrayForEach(obj, arr) {
            printf("receive chan: %d\n", obj->valueint);
            if (obj->valueint > 0 && obj->valueint < 14)
                chans[chan_count ++] = obj->valueint;
        }

        cap_set_chans(chans, chan_count);
        cap_override_state(STATE_AP_SEARCH_START);

    } else if (!strcmp(cmd, CMD_SELECT_AP)) {
        printf("DO AP SELECT\n");
        json = cJSON_Parse(data);
        printf("%s\n",cJSON_Print(json));
        strncpy(bssid_str, cJSON_GetObjectItem(json, "bssid")->valuestring,
            sizeof(bssid_str));
        strncpy((char *)ap.ssid, cJSON_GetObjectItem(json, "ssid")->valuestring,
            sizeof(ap.ssid));
        bssid_str_to_val(bssid_str, ap.bssid);
        ap.channel = cJSON_GetObjectItem(json, "channel")->valueint; 

        if (ctx->registered)
            cap_set_ap(&ap);

        memcpy(&ctx->selected_ap, &ap, sizeof(struct wifi_ap_info)); //hold on to it

    }
    cJSON_free(json);
}

void handle_cmd_id(char *cmd, void *data, unsigned int len)
{   
    if (!strcmp(cmd, SCANNER_REG_ACK)) {
        ctx->registered = 1;
    }
}

void msg_recv_cb(const char *topic, void *data, unsigned int len)
{
    pthread_mutex_lock(&shared.lock);
    int tlen = strlen(topic);

    if (mqtt_is_sub_match(SCANNER_SUB_CMD_ALL, topic)) {
        //cmd is last part of the topic, so we can do a cheeky trick here 
        //since topic group is separated by /
        handle_cmd_all(basename(topic), data, len);
    } else if (mqtt_is_sub_match(SCANNER_SUB_CMD_ID, topic)) {
        //first check failed so we must have got a cmd for our ID
        handle_cmd_id(basename(topic), data, len);
    }
    pthread_mutex_unlock(&shared.lock);
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
        pthread_mutex_lock(&shared.lock);
        if (shared.stop) {
            pthread_mutex_unlock(&shared.lock);
            break;
        }
   
        if (ctx->registered) {
            sprintf(reg_topic.name, "%s/%s", SCANNER_PUB_CMD_READY, ctx->client_id);

            mqtt_publish_topic(reg_topic, empty);
            printf("Client registered.\n");
            // Edge case: crashed, received ap from active scan, but not yet initialized. Set to saved AP.
            //Error handling is later, so no problem if this is null
            printf(MAC_FMT"\n", MAC_BYTES(ctx->selected_ap.bssid));
            memcpy(&cap_ctx->selected_ap, &ctx->selected_ap, sizeof(struct wifi_ap_info));
            pthread_mutex_unlock(&shared.lock);
            return 0;
        }

        mqtt_publish_topic(reg_topic, empty);
        pthread_mutex_unlock(&shared.lock);
        sleep(2);
    }
    printf("Failed to receive reg ack, exit\n");
    return -1;
}

void *mqtt_thread_func(void *arg)
{
    mqtt_run();
    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{ 
    int ret;
    pthread_t mqtt_thread;
    struct sigaction act;    
    topic_t will = {0, 1};
    cap_ctx = NULL;

    act.sa_handler = sig_handler;
    sigaction(SIGINT, &act, NULL);

    signal(SIGTERM, sig_handler);

    pcap_init(PCAP_CHAR_ENC_UTF_8, NULL);

    ctx = malloc(sizeof(struct scanner_client_ctx));
    memset(ctx, 0, sizeof(struct scanner_client_ctx));
    pthread_mutex_init(&shared.lock, NULL);
    ctx->registered = 0;
    shared.stop = 0;

    if (ctx == NULL) {
        fprintf(stderr, "Failed to allocate memory for context\n");
        return EXIT_FAILURE;
    }
    
    parse_args(argc, argv);

    if ((ret = mqtt_setup(ctx->mqtt_conf_path,&msg_recv_cb)))
        goto mqtt_err;

    ctx->client_id = mqtt_get_user();
    prepare_topics(ctx->client_id, ctx->sub_topics);
    
    mqtt_set_sub_topics(ctx->sub_topics);

    sprintf(will.name, "%s/%s", SCANNER_PUB_CMD_CRASH, ctx->client_id);
    
    if ((ret = mqtt_set_will(will)))
        goto mqtt_err;
    
    cap_ctx = malloc(sizeof(struct capture_ctx));
    if (!ctx) {
        printf("%s(): failed to allocate context\n");
        return -1;
    }
    memset(cap_ctx, 0, sizeof(struct capture_ctx));

    pthread_create(&mqtt_thread, NULL, &mqtt_thread_func, NULL);
    
    while (1) {
        if (try_register())
            break;
        
        if (cap_start_capture(cap_ctx, ctx->dev, &msg_send_cb))
            break;
    }
    free(cap_ctx);

    pthread_join(mqtt_thread, NULL);

mqtt_err:
    mqtt_cleanup();
    free(ctx);
    return ret;
}