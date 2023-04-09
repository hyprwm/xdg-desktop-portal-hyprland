#include "utils.h"

char *getFormat(const char *fmt, ...) {
    char *outputStr = NULL;

    va_list args;
    va_start(args, fmt);
    vasprintf(&outputStr, fmt, args);
    va_end(args);

    return outputStr;
}