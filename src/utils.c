
#include "wfs.h"
#include <unistd.h>

void parse_chanlist(char *chanlist, struct wfs_ctx *ctx) {
    char *chanlist_copy = strdup(chanlist);
    printf("test");
    wfs_debug("Chanlist: %s\n", chanlist_copy);
    char *token = strtok(chanlist_copy, ",");
    int i = 0;
    while (token != NULL) {
        ctx->chanlist[i++] = atoi(token);
        token = strtok(NULL, ",");
    }
    ctx->n_chans = i;
    free(chanlist_copy);
}

void parse_args(int argc, char *argv[], struct wfs_ctx *ctx)
{
    char *prog_opts = "d:v:c:";
    int opt;

    while ((opt = getopt(argc, argv, prog_opts)) != -1) {
        switch (opt) {
            case 'd':
                ctx->dev = strdup(optarg);
                wfs_debug("Device: %s\n", ctx->dev);
                break;
            case 'v':
                printf("wfs version %s\n", WFS_VERSION);
                break;
            case 'c':
                parse_chanlist(optarg, ctx);
                break;
            default:
                printf("Usage: %s [-d device] [-v]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    
    if (!ctx->dev) {
        printf("Usage: %s [-d device] [-v] -c CHANNEL1,CHANNEL2...\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    //kazkoks default tokiu atveju
    if(ctx->n_chans == 0) {
        ctx->chanlist[0] = 1;
        ctx->n_chans = 1;
    }

    wfs_debug("Channels: %d\n", ctx->n_chans);
}

struct wfs_ctx *wfs_alloc_ctx() {
    struct wfs_ctx *ctx = malloc(sizeof(struct wfs_ctx));
    memset(ctx, 0, sizeof(struct wfs_ctx));
    return ctx;
}

void wfs_free_ctx(struct wfs_ctx *ctx){
    free(ctx->dev);
    free(ctx);
}

char *wfs_mgmt_frame_to_str(enum frame_subtypes subtype) {
    switch (subtype) {
        case FRAME_SUBTYPE_ASSOC_REQ:
            return "Association Request";
        case FRAME_SUBTYPE_ASSOC_RESP:
            return "Association Response";
        case FRAME_SUBTYPE_REASSOC_REQ:
            return "Reassociation Request";
        case FRAME_SUBTYPE_REASSOC_RESP:
            return "Reassociation Response";
        case FRAME_SUBTYPE_PROBE_REQ:
            return "Probe Request";
        case FRAME_SUBTYPE_PROBE_RESP:
            return "Probe Response";
        case FRAME_SUBTYPE_BEACON:
            return "Beacon";
        case FRAME_SUBTYPE_DISASSOC:
            return "Disassociation";
        case FRAME_SUBTYPE_AUTH:
            return "Authentication";
        case FRAME_SUBTYPE_DEAUTH:
            return "Deauthentication";
        case FRAME_SUBTYPE_ACTION:
            return "Action";
        default:
            return "Unknown";
    }
}

char *wfs_frame_type_to_str(enum frame_types type) {
    switch (type) {
        case FRAME_TYPE_MGMT:
            return "Management";
        case FRAME_TYPE_CTRL:
            return "Control";
        case FRAME_TYPE_DATA:
            return "Data";
        default:
            return "Unknown";
    }
}

void wfs_print_mac(u_int8_t *mac)
{ 
    printf(MAC_FMT"\n", MAC_BYTES(mac));
}
