/* common.h -- shared limits and includes for AIS.
 * Originally 2001; re-engineered 2026: C99, plain text, stack/streaming.
 *
 * Copyright (C) 2001 Vasili Gavrilov. GNU GPL v2 or later.
 */
#ifndef AIS_COMMON_H
#define AIS_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fixed buffers. Peak footprint is a function of these, never of the data
 * size: a 10 GB store and a 10 KB store run in the same memory. */
#define AIS_LINE_MAX   65536   /* one store line: id|ts|keys|value      */
#define AIS_PATH_MAX    4096   /* a built path: dir + "/idx/" + p + key */
#define AIS_KEY_MAX      512   /* one encoded key (a path component)    */
#define AIS_KEYS_MAX      64   /* keys per record / query (merge width) */
#define AIS_TS_MAX        24   /* a save timestamp "YYYY-MM-DDThh:mm:ss" + slack */

/* The "off" id->offset index: one fixed-width line per id, value = (offset+1)
 * so 0 is the "absent" sentinel. 11 digits hold offsets up to ~90 GB. */
#define AIS_OFF_WIDTH     12   /* 11-digit (offset+1) + '\n'            */

/* On-disk format version (INDEX/version). Bump only if the canonical store
 * format changes; derived files (idx/off/multi) are rebuilt by compact.
 *   v1: id|keys|value
 *   v2: id|ts|keys|value   (ts = save time; v2 readers still read v1 lines) */
#define AIS_FORMAT_VERSION 2

#endif /* AIS_COMMON_H */
