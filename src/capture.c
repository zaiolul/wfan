#include "wfs.h"
#include "capture.h"

static double rssi_avg = 0;
static int rssi_count = 0;
static int avg_count = 0;

int wfs_setup_device(struct wfs_ctx *ctx) 
{
    char err_msg[PCAP_ERRBUF_SIZE];

    ctx->handle = pcap_open_live(ctx->dev, CAP_BUF_SIZE, 0,  1, err_msg);
    if (ctx->handle == NULL) {
        fprintf(stderr, "Failed to open device %s: %s\n", ctx->dev, err_msg);
        return PCAP_ERROR;
    }

    return 0;
}

static void wfs_apply_field_pad(enum radiotap_present_flags flag, u_int8_t *offset, u_int8_t **data)
{
    u_int8_t rem = *offset % radiotap_entries[flag].align;

    if (!rem)
        return;

    u_int8_t pad = radiotap_entries[flag].align - rem;
    *offset += pad;
    *data += pad;
}

static int wfs_parse_radiotap(struct radiotap_header *radiotap, struct radio_info *info) 
{
    u_int8_t offset = 0;
    u_int8_t *data = (u_int8_t *)radiotap + sizeof(struct radiotap_header);
    u_int32_t *present_flags = &radiotap->present_flags;
    u_int8_t pad;

    // skip over extended bitmask
    while (*present_flags & (1 << RADIOTAP_EXT)) {       
        data += sizeof(u_int32_t);
        offset += sizeof(u_int32_t);
        present_flags ++;
    }

    if (RADIOTAP_HAS_FLAG(radiotap, RADIOTAP_TSFT)) {
        wfs_apply_field_pad(RADIOTAP_TSFT, &offset, &data);
        
        offset += radiotap_entries[RADIOTAP_TSFT].size;
        data += radiotap_entries[RADIOTAP_TSFT].size;
    }

    if (RADIOTAP_HAS_FLAG(radiotap, RADIOTAP_FLAGS)) {
        //ignored
        offset += radiotap_entries[RADIOTAP_FLAGS].size;
        data += radiotap_entries[RADIOTAP_FLAGS].size;
    }

    if (RADIOTAP_HAS_FLAG(radiotap, RADIOTAP_RATE)) {
        //ignored
        offset += radiotap_entries[RADIOTAP_RATE].size;
        data += radiotap_entries[RADIOTAP_RATE].size;
    }

    if (RADIOTAP_HAS_FLAG(radiotap, RADIOTAP_CHANNEL)) {
        wfs_apply_field_pad(RADIOTAP_CHANNEL, &offset, &data);

        info->channel_freq = *(u_int16_t *)data;
        offset += radiotap_entries[RADIOTAP_CHANNEL].size;
        data += radiotap_entries[RADIOTAP_CHANNEL].size;
        //we do not care about channel flags
    }
    
    if (RADIOTAP_HAS_FLAG(radiotap, RADIOTAP_FHSS)) {
        offset += radiotap_entries[RADIOTAP_FHSS].size;
        data += radiotap_entries[RADIOTAP_FHSS].size;
    }
    
    if (RADIOTAP_HAS_FLAG(radiotap, RADIOTAP_ANTENNA_SIGNAL)) {
        info->antenna_signal = *(int8_t *)data;
        offset += radiotap_entries[RADIOTAP_ANTENNA_SIGNAL].size;
        data += radiotap_entries[RADIOTAP_ANTENNA_SIGNAL].size;
    }

    if (RADIOTAP_HAS_FLAG(radiotap, RADIOTAP_NOISE)) {
        info->noise = *(int8_t *)data;
        offset += radiotap_entries[RADIOTAP_NOISE].size;
        data += radiotap_entries[RADIOTAP_NOISE].size;
    }

    return 0;
}


static int wfs_parse_mgmt_frame(u_int8_t *frame) 
{
    struct wifi_mgmt_header *mgmt = (struct wifi_mgmt_header *)frame;
    switch (mgmt->ctrl.subtype) {
        case FRAME_SUBTYPE_BEACON:
            wfs_print_mac(mgmt->addr1);
            wfs_print_mac(mgmt->addr2);
            wfs_print_mac(mgmt->addr3);
            break;
        //ignore others for now
        default:
            break;
    }
    return 0;
}

static int wfs_parse_data_frame() 
{
    return 0;
}

static int wfs_parse_frame(u_int8_t *frame, size_t len)
{
    // wfs_debug("Frame type: %s subtype: %s id: %d\n", 
    //     wfs_frame_type_to_str(frame->ctrl.type),
    //     frame->ctrl.subtype == FRAME_TYPE_MGMT ? wfs_mgmt_frame_to_str (frame->ctrl.subtype) : "",
    //     frame->id
        
    // );
    if (len < sizeof(struct wifi_frame_control))
        return -1;

    struct wifi_frame_control *ctrl = (struct wifi_frame_control *)frame; 
    
    switch (ctrl->type) {
        case FRAME_TYPE_MGMT:
            wfs_debug("-MANAGEMENT FRAME-\n", NULL);
            wfs_parse_mgmt_frame(frame);
            break;
        case FRAME_TYPE_CTRL:
            wfs_debug("-CONTROL FRAME-\n", NULL);  
            wfs_parse_data_frame();  
            break;
        case FRAME_TYPE_DATA:
            wfs_debug("-DATA FRAME-\n", NULL);
            break;
        default:
            break;
    }
   
    return 0;
}

void wfs_packet_handler(unsigned char *args, const struct pcap_pkthdr *header, const unsigned char *packet) 
{
    struct radiotap_header *radiotap = (struct radiotap_header *)packet;
    struct radio_info info = {0};
    u_int8_t *frame;

    if (header->len < sizeof(struct radiotap_header))
        return;

    if (!radiotap->present_flags || !radiotap->length)
        return;
    
    if (wfs_parse_radiotap(radiotap, &info)) {
        return;
    }
 
    frame = packet + radiotap->length;

    wfs_parse_frame(frame, header->len - radiotap->length); 

    if (!info.antenna_signal)
        return;

    
    rssi_count ++;
    rssi_avg += (info.antenna_signal - rssi_avg) / rssi_count;
    
    printf("info.antenna_signal: %d (%f)\n", info.antenna_signal, rssi_avg - 2.5);
    if (info.antenna_signal < rssi_avg - 2) {
        avg_count ++;
    } else if (avg_count > 0) {
        printf("EXCEEDED AVG FOR %d FRAMES \n", avg_count);
        avg_count = 0;
    }

}

int wfs_start_capture(struct wfs_ctx *ctx) 
{
    while (1)
        pcap_dispatch(ctx->handle, 0, wfs_packet_handler, NULL);
    pcap_close(ctx->handle);
    return 0;
}