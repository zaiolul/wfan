#include "capture.h"
#include "mosquitto_mqtt.h"
#include "utils.h"
#include <fcntl.h>
#include <unistd.h>
#include "cJSON.h"

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
    [STATE_MAX] = {NULL}};

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
    char filter_exp[] = "type mgt subtype beacon";
    bpf_u_int32 net;

    // handle = pcap_open_live(device, CAP_BUF_SIZE, 0, 50, err_msg);
    handle = pcap_create(device, err_msg);          
    pcap_set_snaplen(handle, 65535);

    if (handle == NULL)
    {
        fprintf(stderr, "Failed to create handle: %s\n", err_msg);
        return NULL;
    }

    pcap_set_timeout(handle, 10);

    if (pcap_activate(handle)) {
        fprintf(stderr, "Can't activate pcap handle, exit");
        goto err;
    }

    if (pcap_setnonblock(handle, 1, err_msg)) {
        fprintf(stderr, "Can't set pcap non-blocking, exit");
        goto err;
    }

    if (pcap_compile(handle, &filter, filter_exp, 0, net) == -1)
    {
        fprintf(stderr, "Could not parse filter: %s\n", pcap_geterr(handle));
        goto err;
    }

    if (pcap_setfilter(handle, &filter) == -1)
    {
        fprintf(stderr, "Could not install filter: %s\n", pcap_geterr(handle));
        goto err;
    }

    return handle;
err:
    pcap_close(handle);
    return NULL;
}

void cap_pcap_close(pcap_t *handle)
{
    if (!handle)
        return;

    pcap_close(handle);
}

static int cap_add_ap(struct wifi_ap_info *ap)
{
    for (int i = 0; i < ctx->ap_count; i++) {
        if (bssid_equal(ap->bssid, ctx->ap_list[i].bssid)) {
            return -1;
        }
    }

    if (ctx->ap_count >= AP_MAX)
        return -1;

    printf("ADD AP %s " MAC_FMT " %d\n",
           strlen(ap->ssid) > 0 ? (char *)ap->ssid : "<hidden>",
           MAC_BYTES(ap->bssid), ap->channel);
    memcpy(&(ctx->ap_list[ctx->ap_count++]), ap, sizeof(struct wifi_ap_info));

    return 0;
}

static int cap_add_pkt(struct cap_pkt_info *pkt)
{
    if (ctx->pkt_count >= PKT_MAX)
        return -1;

    memcpy(&(ctx->pkt_list[ctx->pkt_count++]), pkt, sizeof(struct cap_pkt_info));

    return 0;
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

    memcpy(&cap_info->ap.timestamp, &fixed_params->timestamp, sizeof(u_int64_t));
    
    if (ctx->cap_scan_done)
        return;

    while (offset < tag_param_len)
    {
        tag = (struct wifi_tag_param *)ptr;
        ptr += sizeof(struct wifi_tag_param);
        switch (tag->id)
        {
        case TAG_SSID:
            memcpy(cap_info->ap.ssid, ptr, tag->length);
            break;
        case TAG_DS:
            cap_info->ap.channel = *(u_int8_t *)ptr;
            break;
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
    switch (ctrl->subtype) {
    case FRAME_SUBTYPE_BEACON:
        struct wifi_beacon_header *beacon = (struct wifi_beacon_header *)frame;
        u_int8_t *data = (u_int8_t *)beacon + sizeof(struct wifi_beacon_header);
        memcpy(&(cap_info->ap.bssid[0]), &(beacon->addr3[0]), 6);
        cap_parse_beacon_tags(cap_info, data, len - sizeof(struct wifi_beacon_header));
        if (ctx->state == STATE_AP_SEARCH_LOOP)
            cap_add_ap(&cap_info->ap);
        break;
    // ignore others for now
    default:
        break;
    }
}

static void cap_parse_ctrl_frame(struct cap_pkt_info *cap_info, u_int8_t *frame, size_t len)
{
    struct wifi_frame_control *ctrl = (struct wifi_frame_control *)frame;
    switch (ctrl->subtype) {
    case FRAME_SUBTYPE_RTS:
    case FRAME_SUBTYPE_BLOCK_ACK:
        struct wifi_control_has_ta *c = (struct wifi_control_has_ta *)frame;
        memcpy(&(cap_info->ap.bssid[0]), &(c->addr2[0]), 6);
        break;
    default:
        break;
    }
}

static int cap_parse_frame(struct cap_pkt_info *cap_info, u_int8_t *frame, size_t len)
{
    int ret;

    struct wifi_frame_control *ctrl = (struct wifi_frame_control *)frame;

    cap_info->type = ctrl->type;
    cap_info->subtype = ctrl->subtype;

    switch (ctrl->type)
    {
    case FRAME_TYPE_MGMT:
        wfs_debug("-MANAGEMENT FRAME-\n", NULL);
        cap_parse_mgmt_frame(cap_info, frame, len);
        return 0;
        break;
    case FRAME_TYPE_CTRL:
        // cap_parse_ctrl_frame(cap_info, frame, len);
        // break;
    case FRAME_TYPE_DATA:
    default:
        break;
    }
    return -1;
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
    
    frame = packet + radiotap_len;
    if (cap_parse_frame(&cap_info, frame, header->len - radiotap_len))
        return;

    if (ctx->state != STATE_PKT_CAP)
        return;

    if (!bssid_equal(ctx->selected_ap.bssid, cap_info.ap.bssid))
        return;
    cap_add_pkt(&cap_info);
    // printf("added pkt (%d), rssi %d\n", ctx->pkt_count, cap_info.radio.antenna_signal);
}

static char *cap_state_to_str(enum cap_capture_state state)
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

    // should not reach
    return NULL;
}

// bandaid fix to stop work if end received externally
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

static void cap_next_channel()
{
    switch (ctx->cap_band) {
    case BAND_24G:
        if (ctx->cap_channel < 1 && ctx->cap_channel > 14)
            return;
        else if (ctx->cap_channel >= 13) {
            printf("Scan done\n");
            ctx->cap_scan_done = 1;
            return;
        }
        break;
    case BAND_5G:
        if (ctx->cap_channel < 36 && ctx->cap_channel > 165)
            return;
        else if (ctx->cap_channel >= 165) {
            ctx->cap_scan_done = 1;
            return;
        }
        break;
    default:
        return;
    }
    ctx->cap_channel++;

    netlink_switch_chan(&ctx->nl, ctx->cap_channel);
}

static void _do_ap_search_start()
{
    memset(ctx->ap_list, 0, sizeof(struct wifi_ap_info) * AP_MAX);
    memset(&(ctx->selected_ap), 0, sizeof(struct wifi_ap_info));

    ctx->ap_count = 0;
    ctx->cap_band = BAND_24G;
    ctx->cap_channel = 1;
    ctx->cap_scan_done = 0;

    netlink_switch_chan(&ctx->nl, ctx->cap_channel);

    cap_next_state(STATE_AP_SEARCH_LOOP);

    ctx->time = time_millis();
}

static void _do_ap_search_loop()
{
    struct pcap_pkthdr *hdr;
    const u_int8_t *pkt;
    int ret;

    if (ctx->cap_scan_done) {
        if (ctx->ap_count > 0) {
            ctx->payload = AP_LIST;
            cap_next_state(STATE_SEND);
            return;
        }
    }

    u_int64_t elapsed = time_elapsed_ms(ctx->time);
    // printf("%llu ms\n", elapsed);
    if (time_elapsed_ms(ctx->time) >= CHAN_PASSIVE_SCAN_MS) {
        cap_next_channel();
        ctx->time = time_millis();
    }

    ret = pcap_next_ex(ctx->handle,&hdr, &pkt);
    if (ret > 0)
        cap_packet_handler(NULL, hdr, pkt);
    else if (ret < 0)
        fprintf(stderr, "Packet receive error\n");
    //else timeout 

    cap_next_state(STATE_AP_SEARCH_LOOP);
}

static void _do_pkt_cap()
{
    if (ctx->pkt_count == PKT_MAX) {
        ctx->time = time_millis();
        ctx->payload = PKT_LIST;
        cap_next_state(STATE_SEND);
        return;
    }
    pcap_dispatch(ctx->handle, -1, cap_packet_handler, NULL);
    cap_next_state(STATE_PKT_CAP);
}


void ap_list_to_json(cJSON *json, struct wifi_ap_info *ap_list, size_t count)
{
    char bssid[16];
    if (!json)
        return;

    cJSON *list = cJSON_CreateArray();
    for (size_t i = 0; i < count; i++) {
        sprintf(bssid, MAC_FMT, MAC_BYTES(ap_list[i].bssid));
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", (char *)ap_list[i].ssid);
        cJSON_AddStringToObject(ap, "bssid", bssid);
        cJSON_AddNumberToObject(ap, "channel", ap_list[i].channel);
        cJSON_AddItemToArray(list, ap);
        printf("%s\n", cJSON_Print(ap));
    }
    cJSON_AddItemToObject(json, "data", list);
}

void pkt_list_to_json(cJSON *json, struct cap_pkt_info *pkt_list, size_t count)
{
    char bssid[16];
    if (!json)
        return;

    cJSON *list = cJSON_CreateArray();
    for (size_t i = 0; i < count; i++) {
        sprintf(bssid, MAC_FMT, MAC_BYTES(pkt_list[i].ap.bssid));
        cJSON *pkt = cJSON_CreateObject();
        cJSON *radio = cJSON_AddObjectToObject(pkt, "radio");
        cJSON *ap = cJSON_AddObjectToObject(pkt, "ap");

        cJSON_AddNumberToObject(radio, "channel_freq", pkt_list[i].radio.channel_freq);
        cJSON_AddNumberToObject(radio, "antenna_signal", pkt_list[i].radio.antenna_signal);
        cJSON_AddNumberToObject(radio, "noise", pkt_list[i].radio.noise);
        
        cJSON_AddNumberToObject(ap, "channel_freq", pkt_list[i].ap.channel);
        cJSON_AddStringToObject(ap, "ssid", (char *)pkt_list[i].ap.ssid);
        cJSON_AddStringToObject(ap, "bssid", bssid);

        cJSON_AddItemToArray(list, pkt);
    }
    cJSON_AddItemToObject(json, "data", list);
}
static void _do_send()
{
    cJSON *json; 
    cap_state_t next_state;
    cap_msg_t *msg = malloc(sizeof(cap_msg_t));
    msg->type = ctx->payload;

    json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "type", msg->type);

    switch (ctx->payload) {
        case AP_LIST:
            memcpy(msg->ap_list, ctx->ap_list, sizeof(msg->ap_list));
            msg->count = ctx->ap_count;
            cJSON_AddNumberToObject(json, "count", msg->count);
            ap_list_to_json(json, ctx->ap_list, ctx->ap_count);
            ctx->ap_count = 0;
            next_state = STATE_IDLE;
            break;
        case PKT_LIST:
            memcpy(msg->pkt_list, ctx->pkt_list, sizeof(msg->pkt_list));
            msg->count = ctx->pkt_count;
              cJSON_AddNumberToObject(json, "count", msg->count);
              pkt_list_to_json(json, ctx->pkt_list, ctx->pkt_count);
            ctx->pkt_count = 0;
            next_state = STATE_PKT_CAP;
            break;
        default:
            break;
    }

    if (ctx->send_cb) {
        ctx->send_cb(cJSON_Print(json));
        free(msg);
    }

    cJSON_free(json);
    cap_next_state(next_state);
}

static int freq_to_chan(int freq)
{
    if (freq < 2400 || freq > 2500)
        return -1;
    return (freq - 2407) / 5;
}

void cap_set_ap(struct wifi_ap_info *ap)
{
    if (!ap)
        return;
    printf("recv ap: %s\n", ap->ssid);
    memcpy(&ctx->selected_ap, ap, sizeof(struct wifi_ap_info));
    netlink_switch_chan(&ctx->nl, ctx->selected_ap.channel);
    ctx->time = time_millis();
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

    if (netlink_init(&ctx->nl, dev)) {
        fprintf(stderr, "Failed to setup netlink\n");
        return NLE_FAILURE;
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
