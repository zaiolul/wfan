#ifndef MANAGER_H
#define MANAGER_H

#include <libgen.h> //for basename()
#include <unistd.h>
#include "utils.h"
#include <pthread.h>
#include "topics.h"
#include "mosquitto_mqtt.h"
#include "capture_types.h"
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <signal.h>
#include "circ_buf.h"

#define MAX_CLIENTS 8
#define PKT_STATS_BUF_SIZE PKT_MAX 
#define OUTPUT_DIR "./scan_results"
#define RES_FILE_EXT ".csv"
typedef enum state {
    IDLE,
    SEARCHING,
    SELECT_AP,
    CAPTURING
} state_t;

struct scanner_signal_stats {
    int average;
    circ_buf_t *signal_buf;
    circ_buf_t *variance_buf;
    int done;
};

struct scanner_client {
    char id[MAX_ID_LEN];
    unsigned long last_msg;
    struct wifi_ap_info ap_list[AP_MAX];
    size_t ap_count;
    int finished_scan;
    int finished_learn;
    int ready;
    FILE *result_file;
    timer_t crash_timerid;
    struct scanner_signal_stats stats;
};

struct ap_entry {
    struct wifi_ap_info ap;
    int count;
};

struct manager_ctx {
    struct scanner_client clients[MAX_CLIENTS];
    size_t client_count;
    size_t finished_scans;
    size_t ready_clients;
    state_t state;
    state_t prev_state;
    struct wifi_ap_info common_aps[AP_MAX];
    struct wifi_ap_info selected_ap;
    size_t n_cmn_ap;
    int require_input;
    time_t cap_start_time;
};

#endif