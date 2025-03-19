#include "capture.h"
#include "utils.h"
#include <fcntl.h>
#include <unistd.h>

// static double rssi_avg = 0;
// static int rssi_count = 0;
// static int avg_count = 0;

// static FILE *dumpfile;
// #define SNAP_LEN 1300
// #define BUFFER_SIZE 10 

// typedef struct {
//     u_int32_t ts_sec;
//     u_int32_t ts_usec;
//     u_int32_t incl_len;
//     u_int32_t orig_len;
// } record_t;

// typedef struct
// {
//     u_int32_t magic;
//     u_int16_t major;
//     u_int16_t minor;
//     u_int32_t res1;
//     u_int32_t res2;
//     u_int32_t snaplen;
//     u_int32_t linktype;
// } pcap_hdr_t;

// pcap_hdr_t pcap_hdr = {
//     .magic = 0xa1b2c3d4,
//     .major = 2,
//     .minor = 4,
//     .res1 = 0,
//     .res2 = 0,
//     .snaplen = SNAP_LEN,
//     .linktype = 127};
// static int buf_index = 0;

static struct wfs_capture_ctx *ctx;

static cap_state_t _do_idle();
static cap_state_t _do_ap_search_start();
static cap_state_t _do_ap_search_loop();
static cap_state_t _do_pkt_cap();
static cap_state_t _do_send();

typedef cap_state_t (*state_handler)();

state_handler handlers[] = {
    [STATE_IDLE] = {_do_idle},
    [STATE_AP_SEARCH_START] = {_do_ap_search_start},
    [STATE_AP_SEARCH_LOOP] = {_do_ap_search_loop},
    [STATE_PKT_CAP] = {_do_pkt_cap},
    [STATE_SEND] = {_do_send},
    [STATE_MAX] = {NULL}
};

struct radiotap_entry radiotap_entries[] = {
    [RADIOTAP_TSFT] = {8, 8},           // TSFT
    [RADIOTAP_FLAGS] = {1, 1},          // Flags
    [RADIOTAP_RATE] = {1, 1},           // Rate
    [RADIOTAP_CHANNEL] = {2, 4},        // Channel
    [RADIOTAP_FHSS] = {2, 2},           // FHSS
    [RADIOTAP_ANTENNA_SIGNAL] = {1, 1}, // Antenna signal
    [RADIOTAP_NOISE] = {1, 1},          // Noise
};

pcap_t *wfs_pcap_setup(char *device)
{
    char err_msg[PCAP_ERRBUF_SIZE];
    int ret;
    pcap_t *handle;
    struct bpf_program filter;
    char filter_exp[] = "wlan[0] & 0xFC == 0x80";
    bpf_u_int32 net;
    handle = pcap_open_live(device, CAP_BUF_SIZE, 0, -1, err_msg);
    if (handle == NULL)
    {
        fprintf(stderr, "Failed to create handle: %s\n", err_msg);
        return NULL;
    }

    if (pcap_compile(handle, &filter, filter_exp, 0, net) == -1)
    {
        fprintf(stderr, "Could not parse filter: %s\n", pcap_geterr(handle));
        return NULL;
    }
    if (pcap_setfilter(handle, &filter) == -1)
    {
        fprintf(stderr, "Could not install filter: %s\n", pcap_geterr(handle));
        return NULL;
    }

    // unlink("capture.pcap");
    // dumpfile = fopen("capture.pcap", "a");

    // fwrite(&pcap_hdr, sizeof(pcap_hdr_t), 1, dumpfile);
    // wfs_debug("Created pcap handle\n", NULL);

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
    while (*present_flags & (1 << RADIOTAP_EXT))
    {
        data += sizeof(u_int32_t);
        offset += sizeof(u_int32_t);
        present_flags++;
    }

    // ugly but it does it's best :)
    for (int i = RADIOTAP_FIRST; i < RADIOTAP_MAX; i++)
    {

        if (!RADIOTAP_HAS_FLAG(radiotap, i))
            continue;

        wfs_apply_field_pad(i, &offset, &data);

        switch (i)
        {
        case RADIOTAP_ANTENNA_SIGNAL:
            wfs_info->radio.antenna_signal = *(int8_t *)data;
            break;
        case RADIOTAP_CHANNEL:
            wfs_info->radio.channel_freq = *(u_int16_t *)data;
            break;
        case RADIOTAP_NOISE:
            wfs_info->radio.noise = *(int8_t *)data;
            break;
        // add fields as needed
        default:
            break;
        }

        offset += radiotap_entries[i].size;
        data += radiotap_entries[i].size;
    }

    return radiotap->length;
}

static void wfs_parse_beacon_tags(struct wfs_pkt_info *wfs_info, u_int8_t *frame_data, size_t data_len)
{
    struct wifi_beacon_fixed_params *fixed_params = (struct wifi_beacon_fixed_params *)frame_data;
    u_int8_t *ptr = frame_data + sizeof(struct wifi_beacon_fixed_params);
    size_t tag_param_len = data_len - sizeof(struct wifi_beacon_fixed_params);
    struct wifi_tag_param *tag;
    int offset = 0;

    while (offset < tag_param_len)
    {
        tag = (struct wifi_tag_param *)ptr;
        ptr += sizeof(struct wifi_tag_param);
        // wfs_debug("tag->id %d tag->length %d\n", tag->id, tag->length);
        switch (tag->id)
        {
        case TAG_SSID:
            memcpy(wfs_info->ap.ssid, ptr, tag->length);
            // wfs_debug("SSID: %s\n", wfs_info->ap.ssid);
            break;
        default:
            break;
        }
        offset += tag->length + sizeof(struct wifi_tag_param);
        ptr += tag->length;
    }
}

static void wfs_parse_mgmt_frame(struct wfs_pkt_info *wfs_info, u_int8_t *frame, size_t len)
{
    struct wifi_frame_control *ctrl = (struct wifi_frame_control *)frame;
    switch (ctrl->subtype)
    {
    case FRAME_SUBTYPE_BEACON:
        struct wifi_beacon_header *beacon = (struct wifi_beacon_header *)frame;
        u_int8_t *data = (u_int8_t *)beacon + sizeof(struct wifi_beacon_header);
        memcpy(&(wfs_info->ap.bssid[0]), &(beacon->addr3[0]), 6);
        wfs_parse_beacon_tags(wfs_info, data, len - sizeof(struct wifi_beacon_header));
        break;
    // ignore others for now
    default:
        break;
    }
}

static int wfs_parse_frame(struct wfs_pkt_info *wfs_info, u_int8_t *frame, size_t len)
{
    int ret;

    if (len < 24) // smallest wifi mac header size
        return -1;

    struct wifi_frame_control *ctrl = (struct wifi_frame_control *)frame;

    switch (ctrl->type)
    {
    case FRAME_TYPE_MGMT:
        wfs_debug("-MANAGEMENT FRAME-\n", NULL);
        wfs_parse_mgmt_frame(wfs_info, frame, len);
        break;
    case FRAME_TYPE_CTRL:
    case FRAME_TYPE_DATA:
    default:
        break;
    }
    return 0;
}

static int bssid_equal(u_int8_t *a, u_int8_t *b)
{
    for (int i = 0; i < 6; i ++) {
        if (a[i] != b[i])
            return 0;
    }
    
    return 1;
}

static int wfs_add_ap(struct wifi_ap_info *ap)
{
    for (int i = 0; i < ctx->ap_count; i ++) {
        if (bssid_equal(ap->bssid, ctx->ap_list[i].bssid)) {
            wfs_debug("BSSID %s EXISTS IN LIST\n", ap->ssid);
            return -1;
        }
    }

    if (ctx->ap_count < AP_MAX -1)
        memcpy(&(ctx->ap_list[ctx->ap_count++]),ap, sizeof(struct wifi_ap_info));
    else
        return -1;
    wfs_debug("Added AP %s to list\n", ap.ssid);
    return 0;
}

static int wfs_add_pkt(struct wfs_pkt_info *pkt)
{
    memcpy(&(ctx->pkt_list[ctx->pkt_count++]), pkt, sizeof(struct wfs_pkt_info));
    wfs_debug("Added pkt info to list\n", ap.ssid);

    return 0;
}

static void wfs_packet_handler(unsigned char *args, const struct pcap_pkthdr *header, const unsigned char *packet)
{
    struct wfs_pkt_info wfs_info = {0};
    u_int8_t *frame;
    int radiotap_len;

    if (ctx->state == STATE_PKT_CAP && !ctx->selected_ap)
        return;

    if (header->len < sizeof(struct radiotap_header))
        return;

    radiotap_len = wfs_parse_radiotap(&wfs_info, packet);
    if (radiotap_len < 0)
        return;

    if (!wfs_info.radio.antenna_signal)
        return;

    frame = packet + radiotap_len;

    if (wfs_parse_frame(&wfs_info, frame, header->len - radiotap_len))
        return;

    //packet has been parsed, do stuff
    switch(ctx->state) {
        case STATE_AP_SEARCH_LOOP:
            if (wfs_add_ap(&(wfs_info.ap)) < 0) {
                wfs_debug("Failed to add ap %s\n", wfs_info.ap.ssid);
                return;
            }
            printf("added ap %s\n", wfs_info.ap.ssid);
            break;
        case STATE_PKT_CAP:
            if (!bssid_equal(ctx->selected_ap->bssid, wfs_info.ap.bssid))
                return;

            if (wfs_add_pkt(&wfs_info) < 0) {
                wfs_debug("Failed to add ap %s\n", wfs_info.ap.ssid);

                return;
            }
            printf("added pkt (%d), rssi %d\n",ctx->pkt_count, wfs_info.radio.antenna_signal);
        default:
            break;
    }


    // rssi_count ++;
    // rssi_avg += (wfs_info.radio.antenna_signal - rssi_avg) / rssi_count;

    // printf("\"%s\" (%02x:%02x:%02x:%02x:%02x:%02x) signal: %d (%f)\n",
    // wfs_info.ap.ssid,
    // wfs_info.ap.bssid[0], wfs_info.ap.bssid[1], wfs_info.ap.bssid[2], wfs_info.ap.bssid[3], wfs_info.ap.bssid[4], wfs_info.ap.bssid[5],
    // wfs_info.radio.antenna_signal, rssi_avg - 2.5);

    // if (wfs_info.radio.antenna_signal < rssi_avg - 2) {
    //     avg_count ++;
    // } else if (avg_count > 0) {
    //     printf("EXCEEDED AVG FOR %d FRAMES \n", avg_count);
    //     avg_count = 0;
    // }

    // record_t record = {
    //     .ts_sec = header->ts.tv_sec,
    //     .ts_usec = header->ts.tv_usec,
    //     .incl_len = radiotap_len,
    //     .orig_len = radiotap_len,
    // };
    // // printf("header->caplen %d header->len %d\n", header->caplen, header->len);
    // fwrite(&record, sizeof(record_t), 1, dumpfile);
    // fwrite(packet, 1, radiotap_len, dumpfile);
    // fflush(dumpfile);
}

static void wfs_print_ap_list(struct wifi_ap_info *ap_list, size_t count)
{ 
    for (int i = 0; i < count; i ++) {
        printf("'%s' " MAC_FMT "\n", ap_list[i].ssid, MAC_BYTES(ap_list[i].bssid));
    }
}

static char* wfs_capture_state_to_str(enum wfs_capture_state state)
{
    switch (state) {
        case STATE_AP_SEARCH_START:
            return "STATE_AP_SEARCH_START";
        case STATE_AP_SEARCH_LOOP:
            return "STATE_AP_SEARCH_LOOP";
        case STATE_PKT_CAP:
            return "STATE_PKT_CAP";
        case STATE_SEND:
            return "STATE_SEND";
        case STATE_IDLE:
            return "STATE_IDLE";
        default:
            return "UNKNOWN";
        }
    
    //should not reach
    return NULL;
}

static cap_state_t _do_idle()
{
    sleep(1);
    return STATE_IDLE;
}
static cap_state_t _do_ap_search_start()
{
    memset(ctx->ap_list, 0, sizeof(struct wifi_ap_info) * AP_MAX);
    ctx->selected_ap = NULL;
    ctx->ap_count = 0;

    time_t t1 = time(&(ctx->start_time));
    time_t t2 = time(&(ctx->cur_time));
    printf("time1 %f time2 %f\n", t1, t2);
    return STATE_AP_SEARCH_LOOP;
}

static cap_state_t _do_ap_search_loop()
{
    time(&(ctx->cur_time));
    if (difftime(ctx->cur_time, ctx->start_time) > AP_SEARCH_TIME_S) {
        if (ctx->ap_count > 0) {
            ctx->payload = AP_LIST;
            return STATE_SEND;
        }
        else {
            printf("No APs found\n");
            return STATE_IDLE;
        }
    }

    pcap_dispatch(ctx->handle, -1, wfs_packet_handler, NULL);
    return STATE_AP_SEARCH_LOOP;
}

static cap_state_t _do_pkt_cap()
{
    if (ctx->pkt_count == PKT_MAX - 1) {
        ctx->payload = PKT_LIST;
        return STATE_SEND;
    }
    pcap_dispatch(ctx->handle,-1, wfs_packet_handler, NULL);
    return STATE_PKT_CAP;
}

static cap_state_t _do_send()
{
    // struct wfs_send_payload *payload = (struct wfs_send_payload*) arg;
    switch (ctx->payload) {
        case AP_LIST:
            if (ctx->ap_count) {
                ctx->selected_ap = ctx->ap_list;
                return STATE_PKT_CAP;
            }
            return STATE_IDLE;
        break;
        case PKT_LIST:
            ctx->pkt_count = 0;
            return STATE_PKT_CAP;
        break;
    }
    return STATE_IDLE;
}

int wfs_start_capture(char *dev)
{
    ctx = malloc(sizeof(struct wfs_capture_ctx));
    ctx->handle = wfs_pcap_setup(dev);
    if (!ctx->handle) {
        fprintf(stderr, "Failed to setup pcap on device\n");
        return PCAP_ERROR;
    }

    ctx->state = STATE_AP_SEARCH_START;

    wfs_debug("Start packet capture\n", NULL);

    while (1) {
        if (!handlers[ctx->state])
            continue;
        ctx->state = handlers[ctx->state]();
    }
    return 0;
}

int wfs_stop_capture()
{
    wfs_pcap_close(ctx->handle);
    free(ctx);
}