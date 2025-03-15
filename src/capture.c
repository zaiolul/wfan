#include "capture.h"
#include "utils.h"

static double rssi_avg = 0;
static int rssi_count = 0;
static int avg_count = 0;

struct radiotap_entry radiotap_entries[] = {
    [RADIOTAP_TSFT] = {8, 8}, // TSFT
    [RADIOTAP_FLAGS] = {1, 1}, // Flags
    [RADIOTAP_RATE] = {1, 1}, // Rate
    [RADIOTAP_CHANNEL] = {2, 4}, // Channel
    [RADIOTAP_FHSS] = {2, 2}, // FHSS
    [RADIOTAP_ANTENNA_SIGNAL] = {1, 1}, // Antenna signal
    [RADIOTAP_NOISE] = {1, 1}, // Noise
};
 
pcap_t * wfs_pcap_setup(char *device) 
{
    char err_msg[PCAP_ERRBUF_SIZE];
    int ret;
    pcap_t *handle;

    handle = pcap_open_live(device, CAP_BUF_SIZE, 0,  1, err_msg);
    if (handle == NULL) {
        fprintf(stderr, "Failed to create handle: %s\n", err_msg);
        return NULL;
    }

    wfs_debug("Created pcap handle\n", NULL);
    
    return handle;
}

void wfs_pcap_close(pcap_t *handle)
{
    if (!handle)
       return;
    
    pcap_close(handle);
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

static int wfs_parse_radiotap(u_int8_t *packet, struct radio_info *info)
{
    struct radiotap_header *radiotap = (struct radiotap_header *)packet;
    
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

    //ugly but it does it's best :)
    for (int i = RADIOTAP_FIRST; i < RADIOTAP_MAX; i ++) {

        if (!RADIOTAP_HAS_FLAG(radiotap, i))
            continue;

        wfs_apply_field_pad(i, &offset, &data);

        switch(i) {
            case RADIOTAP_ANTENNA_SIGNAL:
                info->antenna_signal = *(int8_t *)data;
                break;
            case RADIOTAP_CHANNEL:
                info->channel_freq = *(u_int16_t *)data;
                break;
            case RADIOTAP_NOISE:
                info->noise = *(int8_t *)data;
                break;
            //add fields as needed
            default:
                break;
        }

        offset += radiotap_entries[i].size;
        data += radiotap_entries[i].size;
    }

    return radiotap->length;
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
    struct radio_info info = {0};
    u_int8_t *frame;
    int ret;
    if (header->len < sizeof(struct radiotap_header))
        return;

    ret = wfs_parse_radiotap(packet, &info);
    if (ret < 0) 
        return;
 
    frame = packet + ret;

    wfs_parse_frame(frame, header->len - ret); 

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

int wfs_start_capture(pcap_t *handle) 
{
    if (!handle)
        return PCAP_ERROR;
    wfs_debug("Start packet capture\n", NULL);

    while (1)
        pcap_dispatch(handle, 0, wfs_packet_handler, NULL);
    pcap_close(handle);
    return 0;
}