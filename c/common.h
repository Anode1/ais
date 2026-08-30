/* common.h -- shared limits and includes for AIS.
 *
 * Copyright (C) 2001 Vasili Gavrilov. GPL-2.0-or-later OR MIT.
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

/* On-disk format version (INDEX/version). Bump when an OLDER binary reading this
 * index would get the data wrong; derived files (idx/off/multi) are rebuilt by
 * compact and never justify a bump.
 *   v1: id|keys|value
 *   v2: id|ts|keys|value          (ts = local save time, no zone)
 *   v3: ts is UTC ISO-8601 with a trailing 'Z' (canonical across devices).
 *   v4: mts/sts/katt carry real data (LAYOUT.md). The store LINE is unchanged,
 *       but a delete's meaning now depends on those files: a v3 binary ignores
 *       mts, so a record edited after another device deleted it loses the edit
 *       and the record on the next sync; ignoring sts re-sends a settled
 *       tombstone every round, forever. Whole-folder sync (Syncthing) shares an
 *       index verbatim, so one un-upgraded device reaches this.
 * A v4 reader still reads v1/v2/v3 lines; an older binary refuses a v4 index
 * rather than corrupt-on-read, exactly as v3 did to v2. Upgrade every device. */
#define AIS_FORMAT_VERSION 4

/* The widest wire frame that can carry a VALUE: "M|<ts:20>|<hash:16>|" plus the
 * newline. A value must fit inside AIS_LINE_MAX with this on top of it, or it
 * cannot travel to a peer even though it fits the store line (whose overhead is
 * the id and the keys instead). store_append refuses one that cannot. */
#define AIS_WIRE_FRAME_MAX 41

/* Keep a big, rarely-taken frame out of its caller's. A static function called
 * once is inlined, reserving its buffers on every call to the caller, including
 * the ones that never reach the branch it sits on; on the record path that is
 * the difference between fitting the 512 KB thread stack the FFI seam runs on
 * and not. A hint only: it compiles to nothing elsewhere and the code is
 * correct either way. */
#if defined(__GNUC__) || defined(__clang__)
#  define AIS_NOINLINE __attribute__((noinline))
#else
#  define AIS_NOINLINE
#endif

#endif /* AIS_COMMON_H */
