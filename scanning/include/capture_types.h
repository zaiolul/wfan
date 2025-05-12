#ifndef CAPTURE_TYPES_H
#define CAPTURE_TYPES_H
#include <sys/types.h>

struct radio_info {
    u_int16_t channel_freq;
    int8_t antenna_signal; 
    int8_t noise;
};
struct wifi_ap_info {
    u_int8_t ssid[32];
    u_int8_t bssid[6];
    u_int64_t timestamp;
    u_int16_t channel; // got from DS params
};

struct cap_pkt_info {
    struct radio_info radio; 
    struct wifi_ap_info ap;
};

#define AP_MAX 50
#define PKT_MAX 10

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
#endif