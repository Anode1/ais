/* help.h -- usage text, kept out of main.c for clarity. */
#ifndef AIS_HELP_H
#define AIS_HELP_H

#include <stdio.h>

void usage_short(FILE *out);   /* synopsis: shown for no-args and -h   */
void usage_long(FILE *out);    /* full help with examples: shown for --help */

#endif /* AIS_HELP_H */
