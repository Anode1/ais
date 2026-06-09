/* log.c -- die() and debug(). See log.h. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "log.h"

int ais_debug_flag = 0;

void die(const char *fmt, ...)
{
    va_list ap;

    fputs("ais: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

void debug(const char *fmt, ...)
{
    va_list ap;

    if (!ais_debug_flag)
        return;
    fputs("ais: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
