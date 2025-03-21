#include "wfs.h"

#define TO_STR_VAL(val) #val
#define TO_STR(val) TO_STR_VAL(val)


void handle_cmd_all(char *cmd)
{

}

void handle_cmd_id(char *cmd, void *data, unsigned int len)
{
    
}

void msg_cb(const char *topic, void *data, unsigned int len)
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
    mqtt_setup(sub_topics, &msg_cb);
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

    cap_start_capture(ctx->dev); // loop

    pthread_join(mqtt_thread, NULL);
    // open_cmd_sock();
    cap_stop_capture();
    wfs_free_ctx(ctx);
    return EXIT_SUCCESS;
}
