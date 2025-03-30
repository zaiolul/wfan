#include "capture.h"
#include "mosquitto_mqtt.h"
#include "utils.h"
#include <fcntl.h>
#include <unistd.h>

static struct capture_ctx *ctx;

static void _do_idle();
static void _do_ap_search_start();
static void _do_ap_search_loop();
static void _do_pkt_cap();
static void _do_send();

typedef void (*state_handler)();

state_handler handlers[] = {
    [STATE_IDLE] = {_do_idle},
    [STATE_AP_SEARCH_START] = {_do_ap_search_start},
    [STATE_AP_SEARCH_LOOP] = {_do_ap_search_loop},
    [STATE_PKT_CAP] = {_do_pkt_cap},
    [STATE_SEND] = {_do_send},
    [STATE_END] = {NULL},
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

pcap_t *cap_pcap_setup(char *device)
{
    char err_msg[PCAP_ERRBUF_SIZE];
    int ret;
    pcap_t *handle;
    struct bpf_program filter;
    char filter_exp[] = "wlan[0] & 0xFC == 0x80"; //beacon 80211 frames only
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

    return handle;
}

void cap_pcap_close(pcap_t *handle)
{
    if (!handle)
        return;

    pcap_close(handle);
}

static void cap_apply_field_pad(enum radiotap_present_flags flag, u_int8_t *offset, u_int8_t **data)
{
    u_int8_t rem = *offset % radiotap_entries[flag].align;

    if (!rem)
        return;

    u_int8_t pad = radiotap_entries[flag].align - rem;
    *offset += pad;
    *data += pad;
}

static int cap_parse_radiotap(struct cap_pkt_info *cap_info, u_int8_t *packet)
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

        cap_apply_field_pad(i, &offset, &data);

        switch (i)
        {
        case RADIOTAP_ANTENNA_SIGNAL:
            cap_info->radio.antenna_signal = *(int8_t *)data;
            break;
        case RADIOTAP_CHANNEL:
            cap_info->radio.channel_freq = *(u_int16_t *)data;
            break;
        case RADIOTAP_NOISE:
            cap_info->radio.noise = *(int8_t *)data;
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

static void cap_parse_beacon_tags(struct cap_pkt_info *cap_info, u_int8_t *frame_data, size_t data_len)
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
        switch (tag->id)
        {
        case TAG_SSID:
            memcpy(cap_info->ap.ssid, ptr, tag->length);
            // break;
            return; //do not really care about any others at this point
        default:
            break;
        }
        offset += tag->length + sizeof(struct wifi_tag_param);
        ptr += tag->length;
    }
}

static void cap_parse_mgmt_frame(struct cap_pkt_info *cap_info, u_int8_t *frame, size_t len)
{
    struct wifi_frame_control *ctrl = (struct wifi_frame_control *)frame;
    switch (ctrl->subtype)
    {
    case FRAME_SUBTYPE_BEACON:
        struct wifi_beacon_header *beacon = (struct wifi_beacon_header *)frame;
        u_int8_t *data = (u_int8_t *)beacon + sizeof(struct wifi_beacon_header);
        memcpy(&(cap_info->ap.bssid[0]), &(beacon->addr3[0]), 6);
        cap_parse_beacon_tags(cap_info, data, len - sizeof(struct wifi_beacon_header));
        break;
    // ignore others for now
    default:
        break;
    }
}

static int cap_parse_frame(struct cap_pkt_info *cap_info, u_int8_t *frame, size_t len)
{
    int ret;

    if (len < 24) // smallest wifi mac header size
        return -1;

    struct wifi_frame_control *ctrl = (struct wifi_frame_control *)frame;

    switch (ctrl->type)
    {
    case FRAME_TYPE_MGMT:
        wfs_debug("-MANAGEMENT FRAME-\n", NULL);
        cap_parse_mgmt_frame(cap_info, frame, len);
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

static int cap_add_ap(struct wifi_ap_info *ap)
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

static int cap_add_pkt(struct cap_pkt_info *pkt)
{
    memcpy(&(ctx->pkt_list[ctx->pkt_count++]), pkt, sizeof(struct cap_pkt_info));
    wfs_debug("Added pkt info to list\n", ap.ssid);

    return 0;
}

static void cap_packet_handler(unsigned char *args, const struct pcap_pkthdr *header, const unsigned char *packet)
{
    struct cap_pkt_info cap_info = {0};
    u_int8_t *frame;
    int radiotap_len;

    if (ctx->state == STATE_PKT_CAP && !is_valid_mac(ctx->selected_ap.bssid))
        return;

    if (header->len < sizeof(struct radiotap_header))
        return;

    radiotap_len = cap_parse_radiotap(&cap_info, packet);
    if (radiotap_len < 0)
        return;

    if (!cap_info.radio.antenna_signal)
        return;

    frame = packet + radiotap_len;

    if (cap_parse_frame(&cap_info, frame, header->len - radiotap_len))
        return;

    //packet has been parsed, do stuff
    switch(ctx->state) {
        case STATE_AP_SEARCH_LOOP:
            if (cap_add_ap(&(cap_info.ap)) < 0) {
                wfs_debug("Failed to add ap %s\n", cap_info.ap.ssid);
                return;
            }
            printf("added ap %s\n", cap_info.ap.ssid);
            break;
        case STATE_PKT_CAP:
            if (!bssid_equal(ctx->selected_ap.bssid, cap_info.ap.bssid))
                return;

            if (cap_add_pkt(&cap_info) < 0) {
                printf("Failed to add ap %s\n", cap_info.ap.ssid);

                return;
            }
            printf("added pkt (%d), rssi %d\n",ctx->pkt_count, cap_info.radio.antenna_signal);
        default:
            break;
    }
}


static char* cap_state_to_str(enum cap_capture_state state)
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

//bandaid fix to stop work if end received externally
static void cap_next_state(cap_state_t state)
{
    if (!ctx)
        return;

    if (!ctx->override_state) 
        ctx->state = state;
    ctx->override_state = 0;
}

void cap_override_state(cap_state_t state) 
{
    if (!ctx)
        return;

    ctx->override_state = 1;
    ctx->state = state;
}

static void _do_idle()
{
    sleep(1);
    cap_next_state(STATE_IDLE);
}
static void _do_ap_search_start()
{
    memset(ctx->ap_list, 0, sizeof(struct wifi_ap_info) * AP_MAX);
    memset(&(ctx->selected_ap), 0, sizeof(struct wifi_ap_info));
    ctx->ap_count = 0;

    time_t t1 = time(&(ctx->start_time));
    time_t t2 = time(&(ctx->cur_time));
    cap_next_state(STATE_AP_SEARCH_LOOP);
}

static void _do_ap_search_loop()
{
    time(&(ctx->cur_time));
    if (difftime(ctx->cur_time, ctx->start_time) > AP_SEARCH_TIME_S) {
        if (ctx->ap_count > 0) {
            ctx->payload = AP_LIST;
            cap_next_state(STATE_SEND);
            return;
        }
        else {
            printf("No APs found\n");

            cap_next_state(STATE_IDLE);
            return;
        }
    }

    pcap_dispatch(ctx->handle, -1, cap_packet_handler, NULL);
    cap_next_state(STATE_AP_SEARCH_LOOP);
}

static void _do_pkt_cap()
{
    if (ctx->pkt_count == PKT_MAX - 1) {
        ctx->payload = PKT_LIST;
        cap_next_state(STATE_SEND);
        return;
    }
    pcap_dispatch(ctx->handle,-1, cap_packet_handler, NULL);
    cap_next_state(STATE_PKT_CAP);
}

static void _do_send()
{
    cap_msg_t *msg = malloc(sizeof(cap_msg_t));
    msg->type = ctx->payload;

    cap_state_t next_state;
    switch (ctx->payload) {
        case AP_LIST:
            memcpy(msg->ap_list, ctx->ap_list, sizeof(msg->ap_list));
            msg->count = ctx->ap_count;
            next_state = STATE_IDLE;
            break;
        case PKT_LIST:
            memcpy(msg->pkt_list, ctx->pkt_list, sizeof(msg->pkt_list));
            msg->count = ctx->pkt_count;
            next_state = STATE_PKT_CAP;
            break;
        default:
            break;
    }

    if (ctx->send_cb) {
        ctx->send_cb(msg);
        free(msg);
    }

    cap_next_state(next_state);
}

//used externally, on some event that isn't handled here
void cap_set_ap(struct wifi_ap_info *ap)
{
    if (!ap)
        return;
    printf("recv ap: %s\n", ap->ssid);
    memcpy(&ctx->selected_ap, ap, sizeof(struct wifi_ap_info));
    cap_override_state(STATE_PKT_CAP);
}


int cap_start_capture(char *dev, cap_send_cb cb)
{
    ctx = malloc(sizeof(struct capture_ctx));
    if (!ctx) {
        printf("%s(): failed to allocate context\n");
        return -1;
    }
    memset(ctx, 0, sizeof(struct capture_ctx));

    ctx->handle = cap_pcap_setup(dev);
    ctx->send_cb = cb;
    if (!ctx->handle) {
        fprintf(stderr, "Failed to setup pcap on device\n");
        return PCAP_ERROR;
    }

    cap_next_state(STATE_IDLE);

    while (ctx->state != STATE_END) {
        if (!handlers[ctx->state])
            continue;
        handlers[ctx->state]();
        ctx->override_state = 0;
    }

    cap_pcap_close(ctx->handle);
    free(ctx);
    ctx = NULL;
    return 0;
}

