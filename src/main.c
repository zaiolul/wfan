#include "wfs.h"

void *cap_thread_func(void *arg)
{
    struct wfs_ctx *ctx = (struct wfs_ctx *)arg;
    wfs_start_capture(ctx->dev);
    wfs_stop_capture();
    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{ 
    pcap_init(PCAP_CHAR_ENC_UTF_8, NULL);
    wfs_debug("--START--\n", NULL);
    struct wfs_ctx *ctx = wfs_alloc_ctx();
    pthread_t cap_thread, comm_thread;

    if (ctx == NULL) {
        fprintf(stderr, "Failed to allocate memory for context\n");
        return EXIT_FAILURE;
    }

    parse_args(argc, argv, ctx);
    // pthread_create(&cap_thread, NULL, &cap_thread_func, ctx);
    //disable for now
    if (mqtt_run())
        return EXIT_FAILURE;

    // wfs_start_capture(ctx->dev); // loop
    // pthread_join(cap_thread, NULL);
    // open_cmd_sock();
    // wfs_stop_capture();
    wfs_free_ctx(ctx);
    return EXIT_SUCCESS;
}
