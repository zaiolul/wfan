#include "capture.h"
#include "utils.h"
       #include <fcntl.h>           
       #include <unistd.h>

static double rssi_avg = 0;
static int rssi_count = 0;
static int avg_count = 0;

static FILE *dumpfile;
#define SNAP_LEN 1300
#define BUFFER_SIZE 10  
typedef struct {
    u_int32_t ts_sec;
    u_int32_t ts_usec;
    u_int32_t incl_len;
    u_int32_t orig_len;
} record_t;

typedef struct {
    u_int32_t magic;
    u_int16_t major;
    u_int16_t minor;
    u_int32_t res1;
    u_int32_t res2;
    u_int32_t snaplen;
    u_int32_t linktype;
} pcap_hdr_t;

pcap_hdr_t pcap_hdr = {
    .magic = 0xa1b2c3d4,
    .major = 2,
    .minor = 4,
    .res1 = 0,
    .res2 = 0,
    .snaplen = SNAP_LEN,
    .linktype = 127
};
static int buf_index = 0;

#define STR(x) #x     
#define VAL_TO_STR(x) STR(x)

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
    struct bpf_program filter;
    char filter_exp[] = "wlan[0] & 0xFC == 0x80";
    bpf_u_int32 net;
    handle = pcap_open_live(device, CAP_BUF_SIZE, 0, -1, err_msg);
    if (handle == NULL) {
        fprintf(stderr, "Failed to create handle: %s\n", err_msg);
        return NULL;
    }

    if (pcap_compile(handle, &filter, filter_exp, 0, net) == -1) {
        fprintf(stderr, "Could not parse filter: %s\n", pcap_geterr(handle));
        return NULL;
    }
    if (pcap_setfilter(handle, &filter) == -1) {
        fprintf(stderr, "Could not install filter: %s\n", pcap_geterr(handle));
        return NULL;
    }

    unlink("capture.pcap");
    dumpfile = fopen("capture.pcap", "a");
   
    fwrite(&pcap_hdr, sizeof(pcap_hdr_t), 1, dumpfile);
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

static int wfs_parse_radiotap(struct wfs_pkt_info *wfs_info, u_int8_t *packet)
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
                wfs_info->radio.antenna_signal = *(int8_t *)data;
                break;
            case RADIOTAP_CHANNEL:
                wfs_info->radio.channel_freq = *(u_int16_t *)data;
                break;
            case RADIOTAP_NOISE:
                wfs_info->radio.noise = *(int8_t *)data;
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

static int wfs_parse_tags(struct wfs_pkt_info *wfs_info, u_int8_t *frame_data, size_t data_len) 
{
    struct wifi_beacon_fixed_params *fixed_params = (struct wifi_beacon_fixed_params *)frame_data;
    u_int8_t *ptr = frame_data + sizeof(struct wifi_beacon_fixed_params);
    size_t tag_param_len = data_len - sizeof(struct wifi_beacon_fixed_params);
    struct wifi_tag_param *tag;
    int offset = 0;
    
    
    while (offset < tag_param_len) {
        tag = (struct wifi_tag_param *)ptr;
        ptr += sizeof(struct wifi_tag_param);
        wfs_debug("tag->id %d tag->length %d\n", tag->id, tag->length);
        switch (tag->id) {
            case TAG_SSID:
                memcpy(wfs_info->ap.ssid, ptr, tag->length);
                wfs_debug("SSID: %s\n", buf);
                break;
            default:
                break;
        }
        offset += tag->length + sizeof(struct wifi_tag_param);
        ptr += tag->length;
    }

    return 0;
}

static int wfs_parse_mgmt_frame(struct wfs_pkt_info *wfs_info, u_int8_t *frame, size_t len) 
{
    struct wifi_frame_control *ctrl = (struct wifi_frame_control *)frame; 
    switch (ctrl->subtype) {
        case FRAME_SUBTYPE_BEACON:
            struct wifi_beacon_header *beacon = (struct wifi_beacon_header *)frame;
            u_int8_t *data = (u_int8_t *)beacon + sizeof(struct wifi_beacon_header);
            wfs_info->id = ctrl->type | ctrl->subtype;
            memcpy(wfs_info->ap.bssid, beacon->addr3, 6);
            wfs_parse_tags(wfs_info, data, len - sizeof(struct wifi_beacon_header));
            break;
        //ignore others for now
        default:
            break;
    }
    return 0;
}

static int wfs_parse_frame(struct wfs_pkt_info *wfs_info, u_int8_t *frame, size_t len)
{
    int ret;

    if (len < sizeof(struct wifi_frame_control))
        return -1;
    struct wifi_frame_control *ctrl = (struct wifi_frame_control *)frame; 
    
    switch (ctrl->type) {
        case FRAME_TYPE_MGMT:
            wfs_debug("-MANAGEMENT FRAME-\n", NULL);
            ret = wfs_parse_mgmt_frame(wfs_info, frame, len);
            break;
        case FRAME_TYPE_CTRL:
        case FRAME_TYPE_DATA:
        default:
            break;
    }
   
    return ret;
}

void wfs_packet_handler(unsigned char *args, const struct pcap_pkthdr *header, const unsigned char *packet) 
{
    struct wfs_pkt_info wfs_info = {0};
    u_int8_t *frame;
    int radiotap_len;
    if (header->len < sizeof(struct radiotap_header))
        return;

    radiotap_len = wfs_parse_radiotap(&wfs_info, packet);
    if (radiotap_len < 0) 
        return;
        
    if (!wfs_info.radio.antenna_signal)
        return;

    frame = packet + radiotap_len;

    wfs_parse_frame(&wfs_info, frame, header->len - radiotap_len); 


    rssi_count ++;
    rssi_avg += (wfs_info.radio.antenna_signal - rssi_avg) / rssi_count;
    
    printf("\"%s\" (%02x:%02x:%02x:%02x:%02x:%02x) signal: %d (%f)\n",
    wfs_info.ap.ssid,
    wfs_info.ap.bssid[0], wfs_info.ap.bssid[1], wfs_info.ap.bssid[2], wfs_info.ap.bssid[3], wfs_info.ap.bssid[4], wfs_info.ap.bssid[5],
    wfs_info.radio.antenna_signal, rssi_avg - 2.5);

    if (wfs_info.radio.antenna_signal < rssi_avg - 2) {
        avg_count ++;
    } else if (avg_count > 0) {
        printf("EXCEEDED AVG FOR %d FRAMES \n", avg_count);
        avg_count = 0;
    }

    record_t record = {
        .ts_sec = header->ts.tv_sec,
        .ts_usec = header->ts.tv_usec,
        .incl_len = radiotap_len,
        .orig_len = radiotap_len,
    };
    // printf("header->caplen %d header->len %d\n", header->caplen, header->len);
    fwrite(&record, sizeof(record_t), 1, dumpfile);
    fwrite(packet, 1, radiotap_len, dumpfile);
    fflush(dumpfile);

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