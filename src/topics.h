#ifndef TOPICS_H
#define TOPICS_H

#define TOPIC_CMD_BASE "cmd"
#define TOPIC_CMD_ALL TOPIC_CMD_BASE"/all"
#define TOPIC_DATA_BASE "data"

#define CMD_ALL_STOP "/stop"
#define CMD_SELECT "/select"

#define DATA_PKT "/pkt"
#define DATA_APLIST "/aplist"

#define SCANNER_SUB_CMD_ALL TOPIC_CMD_ALL"/+"

#define SCANNER_SUB_CMD_ID TOPIC_CMD_BASE"/+/+"

#define SCANNER_PUB_CMD_REGISTER TOPIC_CMD_BASE"/+/register"

#define MANAGER_SUB_DATA_PKT TOPIC_DATA_BASE"/+/"DATA_PKT
#define MANAGER_SUB_DATA_AP_LIST TOPIC_DATA_BASE"/+/"DATA_APLIST
#define MANAGER_PUB_CMD_SELECT TOPIC_CMD_ALL CMD_SELECT

typedef struct topic_handler {
    char *topic;
    void* func;
} topic_handler_t;

#endif TOPICS_H