#include <stdarg.h>
#include <stdio.h>
#include "utils.h"

#define ERR_BUF 4096

char error_msg[ERR_BUF] = {0};

void set_error_msg(const char *fmt, ...) {
    wfs_debug("%s()\n", __func__);
    va_list args;
    va_start(args, fmt);
    vsnprintf(error_msg, ERR_BUF, fmt, args);
    va_end(args);
}