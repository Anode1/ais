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

/* On-disk format version (INDEX/version). Bump when an OLDER binary reading this
 * index would get the data wrong; derived files (idx/off/multi) are rebuilt by
 * compact and never justify a bump.
 *   v1: id|keys|value
 *   v2: id|ts|keys|value          (ts = local save time, no zone)
 *   v3: ts is UTC ISO-8601 with a trailing 'Z' (canonical across devices).
 *   v4: mts/sts/katt carry real data (LAYOUT.md). The store LINE is unchanged,
 *       which is why this looked like it needed no bump -- but the meaning of a
 *       delete now depends on those files: a v3 binary ignores mts, so a record
 *       edited after another device deleted it loses the edit and the record,
 *       silently, on the next sync; and ignoring sts makes it re-send a
 *       tombstone the mesh already settled, every round, forever. An index is
 *       shared verbatim by whole-folder sync (Syncthing), which is the setup
 *       doc/SYNC.md recommends, so this is reachable by an ordinary user with
 *       one un-upgraded device. Refusing to open is the only protection an old
 *       binary can apply, since only its own check runs.
 * A v4 reader still reads v1/v2/v3 lines; an older binary refuses a v4 index
 * rather than corrupt-on-read, exactly as v3 did to v2. Upgrade every device. */
#define AIS_FORMAT_VERSION 4

/* Keep a big, RARELY-TAKEN frame out of its caller's.
 *
 * A static function called once is inlined, and its buffers are then reserved on
 * every call to the caller -- including the calls that never reach the branch it
 * sits on. On the record path that is the difference between a frame that fits
 * the 512 KB thread stack the FFI seam runs on and one that does not, so it is
 * worth saying out loud rather than hoping the optimiser agrees.
 *
 * A hint, not a dependency: it compiles to nothing on a toolchain that does not
 * know it, and the code is correct either way. STYLE.md's "a stock C99 toolchain
 * builds it" still holds. */
#if defined(__GNUC__) || defined(__clang__)
#  define AIS_NOINLINE __attribute__((noinline))
#else
#  define AIS_NOINLINE
#endif

#endif /* AIS_COMMON_H */
