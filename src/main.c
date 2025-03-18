#include "wfs.h"

void *cap_thread_func(void *arg)
{
    struct wfs_scan_ctx *ctx = (struct wfs_scan_ctx *)arg;
    wfs_start_capture(ctx->handle);
    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{ 
    printf(pcap_lib_version());
    pcap_init(PCAP_CHAR_ENC_UTF_8, NULL);
    wfs_debug("--START--\n", NULL);
    struct wfs_scan_ctx *ctx = wfs_alloc_ctx();
    pthread_t cap_thread, comm_thread;
    if (ctx == NULL) {
        fprintf(stderr, "Failed to allocate memory for context\n");
        return EXIT_FAILURE;
    }

    parse_args(argc, argv, ctx);
    ctx->handle = wfs_pcap_setup(ctx->dev);
    if (!ctx->handle) {
        fprintf(stderr, "Failed to setup pcap on device\n");
        return EXIT_FAILURE;
    }

    // pthread_create(&cap_thread, NULL, &cap_thread_func, ctx);
    //disable for now
    // if (wfs_mqtt_run())
    //     return EXIT_FAILURE;

    wfs_start_capture(ctx->handle); // loop
    wfs_pcap_close(ctx->handle);

    // open_cmd_sock();
    wfs_free_scan_ctx(ctx);
    return EXIT_SUCCESS;
}
