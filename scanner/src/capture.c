#include "wfs.h"
#include <pcap.h>

int wfs_setup_device(struct wfs_ctx *ctx) 
{
    char err_msg[PCAP_ERRBUF_SIZE];

    ctx->handle = pcap_open_live(ctx->dev, CAP_BUF_SIZE, 0,  1000, err_msg);
    if (ctx->handle == NULL) {
        fprintf(stderr, "Failed to open device %s: %s\n", ctx->dev, err_msg);
        return PCAP_ERROR;
    }


    return 0;
}

void wfs_packet_handler(unsigned char *args, const struct pcap_pkthdr *header, const unsigned char *packet) 
{
    struct hdr_radiotap *radiotap = (struct hdr_radiotap *)packet;
    u_int16_t *frame_ptr = (u_int16_t*)(packet + sizeof(struct hdr_radiotap) - 2);

    wfs_debug("Packet captured: freq: %d kHz signal: %d dbm type: %s subtype: %s \n",
        radiotap->data.channel_freq,
        radiotap->data.antenna_signal,
        wfs_frame_type_to_str(FRAME_CTRL_TYPE(frame_ptr)),
        FRAME_CTRL_TYPE(frame_ptr) == FRAME_TYPE_MGMT ? wfs_mgmt_frame_to_str(FRAME_CTRL_SUBTYPE(frame_ptr)) : "");
}

int wfs_start_capture(struct wfs_ctx *ctx) 
{
    struct pcap_pkthdr header = {0};
    char err_msg[PCAP_ERRBUF_SIZE];

    while (1)
        pcap_dispatch(ctx->handle, 0, wfs_packet_handler, NULL);
    pcap_close(ctx->handle);
    return 0;
}