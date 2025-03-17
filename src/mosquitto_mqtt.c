#include "mosquitto_mqtt.h"
#include <sys/types.h>
#include "utils.h"

static void wfs_mqtt_on_message(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *msg)
{
    // time_t now;
    // time(&now);
    // struct tm *tm_now = localtime(&now);
    // char buff[100];
    // strftime(buff, sizeof(buff), "%Y-%m-%d %H:%M:%S", tm_now) ;
    //log_to_file(msg->topic, (char*)msg->payload, LOG_FILE);

    // int ret = check_events(msg->topic, (char*)msg->payload);
    // switch(ret){
    //     case JSON_ERR:
    //         break;
    //     case EVENT_CONFIG_ERR:
    //         syslog(LOG_ERR, "Error in events config, exiting");
    //         run = 0;
    //     default:{0}
    //         break;
    // }
}

static void wfs_mqtt_on_connect(struct mosquitto *mosq, void *obj, int reason_code)
{
    // syslog(LOG_INFO,  mosquitto_connack_string(reason_code));
    // struct arguments args = *(struct arguments*)obj;
    
	// if(reason_code != 0){
    //     run = 0;
    //     return;
	// }
    // int ret = subscribe_all_topics();
    // if(ret <= 0){
    //     syslog(LOG_INFO, "No topics subscribed, exiting");
    //     run = 0;
    // }
}

static void wfs_mqtt_config_cleanup(struct mosquitto_conf *config)
{
    free(config->host);
    free(config->username);
    free(config->password);
    free(config);
}

static void wfs_mqtt_cleanup(struct mosquitto *mosquitto, struct mosquitto_conf *config) {
    mosquitto_destroy(mosquitto);
    mosquitto_lib_cleanup();
    wfs_mqtt_config_cleanup(config);
}

static struct mosquitto_conf *wfs_mqtt_read_config()
{
    struct mosquitto_conf *conf = malloc(sizeof(struct mosquitto_conf)); 

    FILE *fp = fopen(MQTT_CONFIG_FILE, "r");
    size_t len;
    ssize_t count;
    char *line = NULL, *key, *value;

    //for strtol
    char *end;
    long val;
    
    if(fp == NULL) {
        printf("Can't open mqtt config file\n");
        return NULL;
    }

    while((count = getline(&line, &len, fp)) != -1) {
        if (line[count - 1] == '\n')
            line[count - 1] = '\0';

        key = strtok(line, "=");
        value = strtok(NULL, "=");

        if (!value || strlen(value) == 0)
            continue;
        wfs_debug("Key: '%s', Value: '%s'\n", key, value);
        
        if(strcmp(key, "HOST") == 0) {
            conf->host = strdup(value);
            conf->host[count - 1] = '\0';
        } else if(strcmp(key, "PORT") == 0) {
            conf->port = (int)strtol(value, &end, 10);
        } else if(strcmp(key, "USERNAME") == 0) {
            conf->username = strdup(value);
            conf->username[count - 1] = '\0';
        } else if(strcmp(key, "PASSWORD") == 0) {
            conf->password = strdup(value);
            conf->password[count - 1] = '\0';
        } else {
            printf("Unknown key in config file: %s\n", key);
        }
    }

    wfs_debug("Host: %s, port: %d user: %s pass: %s\n", conf->host, conf->port, conf->username, conf->password);
    free(line);

    fclose(fp);

    if (!conf->host || !conf->port) {
        printf(" Broker host or port not set, check config.\n");
        wfs_mqtt_config_cleanup(conf);
        return NULL;
    }

    return conf;
}

static int mqtt_setup_login(struct mosquitto *mosquitto, struct mosquitto_conf *config)
{
    int ret;
    
    //safe to not check user/pass
    if ((ret = mosquitto_username_pw_set(mosquitto, config->username, config->password)) != MOSQ_ERR_SUCCESS) {
        // syslog(LOG_ERR, "User settings error");
        printf("User settings error (%d)\n", ret);
        return ret;
    }
    return MOSQ_ERR_SUCCESS;
}
//TODO mosquitto tls setup

static int wfs_mqtt_setup(struct mosquitto **mosquitto, struct mosquitto_conf **config)
{
    int ret;

    *config = wfs_mqtt_read_config();

    if (!*config) 
        return MOSQ_ERR_INVAL;

    if((ret = mosquitto_lib_init()) != MOSQ_ERR_SUCCESS){
        printf("Can't initialize mosquitto lib\n");
        return ret;
    }

    *mosquitto = mosquitto_new(NULL, true, NULL);
    if ((ret = mqtt_setup_login(*mosquitto, *config)) != MOSQ_ERR_SUCCESS)
        return ret;
    
    mosquitto_connect_callback_set(*mosquitto, wfs_mqtt_on_connect);
    mosquitto_message_callback_set(*mosquitto, wfs_mqtt_on_message);


    if ((ret = mosquitto_connect(*mosquitto, (*config)->host, (*config)->port, 60)) != MOSQ_ERR_SUCCESS) {
        printf("Can't connect to broker\n");
        return ret;
    }

    return MOSQ_ERR_SUCCESS;
}

int wfs_mqtt_run()
{
    struct mosquitto *mosquitto;
    struct mosquitto_conf *config;

    int ret;
    ret = wfs_mqtt_setup(&mosquitto, &config);

    if (ret != MOSQ_ERR_SUCCESS) {
        printf("Error setting up mosquitto\n");
        return ret;
    }

    wfs_mqtt_cleanup(mosquitto, config);
    
}