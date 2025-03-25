#ifndef CAPTURE_H
#define CAPTURE_H

#include <sys/types.h>
#include <pcap/pcap.h>
#include <time.h>

#define CAP_BUF_SIZE 8096

typedef enum cap_capture_state {
    STATE_IDLE,
    STATE_AP_SEARCH_START,
    STATE_AP_SEARCH_LOOP,
    STATE_PKT_CAP,
    STATE_SEND,
    STATE_END,
    STATE_MAX,
} cap_state_t;

enum radiotap_present_flags {
    RADIOTAP_TSFT = 0,
    RADIOTAP_FLAGS = 1,
    RADIOTAP_RATE = 2,
    RADIOTAP_CHANNEL = 3,
    RADIOTAP_FHSS = 4,
    RADIOTAP_ANTENNA_SIGNAL = 5,
    RADIOTAP_NOISE = 6,
    
    RADIOTAP_EXT = 31,
};

#define RADIOTAP_MAX  RADIOTAP_NOISE + 1 // Don't care about others
#define RADIOTAP_FIRST RADIOTAP_TSFT

struct radiotap_entry {
    u_int8_t align;
    u_int8_t size;
}__attribute__((packed));

struct radiotap_header {
    u_int8_t version;
    u_int8_t padding;
    u_int16_t length;
    u_int32_t present_flags;
}__attribute__((packed));

#define RADIOTAP_HAS_FLAG(hdr, flag) (hdr->present_flags & (1 << flag))

//has to be this way due to endianess?
struct wifi_frame_control {
    u_int16_t version : 2;
    u_int16_t type : 2;
    u_int16_t subtype : 4;
    u_int16_t flags : 8;
}__attribute__((packed));

struct wifi_beacon_header {
    struct wifi_frame_control ctrl;
    u_int16_t id;
    u_int8_t addr1[6];
    u_int8_t addr2[6];
    u_int8_t addr3[6];
    u_int16_t seq_ctl;
}__attribute__((packed));

struct wifi_data_header {
    struct wifi_frame_control ctrl;
    u_int16_t id;
    u_int8_t addr1[6];
    u_int8_t addr2[6];
    u_int8_t addr3[6];
    u_int16_t seq_ctl;
}__attribute__((packed));

struct wifi_data_qos_header {
    struct wifi_frame_control ctrl;
    u_int16_t id;
    u_int8_t addr1[6];
    u_int8_t addr2[6];
    u_int8_t addr3[6];
    u_int16_t seq_ctl;
    u_int16_t qos_ctl;
}__attribute__((packed));

struct wifi_data_frame_header {
    struct wifi_frame_control ctrl;
    u_int16_t id;
    u_int8_t addr1[6];
    u_int8_t addr2[6];
    u_int8_t addr3[6];
    u_int16_t seq_ctl;
}__attribute__((packed));

struct wifi_beacon_fixed_params {
    u_int64_t timestamp;
    u_int16_t interval;
    u_int16_t capabilities;
}__attribute__((packed));

struct wifi_tag_param {
    u_int8_t id;
    u_int8_t length;
}__attribute__((packed));

enum tagged_params {
    TAG_SSID = 0x00,
    //expand if needed
};

struct radio_info {
    u_int16_t channel_freq;
    int8_t antenna_signal;
    int8_t noise;
};
struct wifi_ap_info {
    u_int8_t ssid[32];
    u_int8_t bssid[6];
    u_int64_t timestamp;
};

struct cap_pkt_info {
    struct radio_info radio; 
    struct wifi_ap_info ap;
};

#define AP_MAX 32
#define PKT_MAX 128

typedef enum cap_send_payload_type {
    AP_LIST,
    PKT_LIST,
} cap_payload_t;

struct cap_msg {
    cap_payload_t type;
    size_t count;
    // size_t bytes_len;
    union {
        struct wifi_ap_info ap_list[AP_MAX];
        struct cap_pkt_info pkt_list[PKT_MAX];
    };
} __attribute__((packed));
typedef struct cap_msg cap_msg_t;

typedef void (*cap_send_cb)(cap_msg_t *msg);

struct capture_ctx {
    struct wifi_ap_info ap_list[AP_MAX];
    size_t ap_count;
    struct cap_pkt_info pkt_list[PKT_MAX];
    size_t pkt_count;
    enum cap_send_payload_type payload;
    u_int32_t payload_len;
    enum cap_capture_state state;
    enum cap_capture_state prev_state;

    struct wifi_ap_info selected_ap;
    pcap_t *handle;

    time_t start_time, cur_time;

    cap_send_cb send_cb;
    int override_state;
};

#define FRAME_ID(type, subtype) (type | subtype << 4)

#define RADIOTAP_BAND_24(hdr) (hdr->data.channel_flags & (1 << 7)) 

#define RADIOTAP_BAND_5(hdr) (hdr->data.channel_flags & (1 << 8)) 

#define AP_SEARCH_TIME_S 3
#define IDLE_TIME 60

int cap_start_capture(char *dev, cap_send_cb cb);
void cap_override_state(cap_state_t state);
void cap_set_ap(struct wifi_ap_info *ap);
#endif