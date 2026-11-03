/* due.h -- the shape of a reminder key, for the front ends that warn about a
 * key that will never come due. The engine's own entry point is ais_due() in
 * ais.h; this header carries only what the CLI and the MCP server need to say
 * why a key is not a reminder before it is stored. */
#ifndef AIS_DUE_H
#define AIS_DUE_H

/* The sentence every front end appends to a malformed key's name. */
#define DUE_SHAPE_HINT \
    "is not a date, so it never comes due; a reminder is due:YYYY[-MM[-DD]]"

/* Is KEY a reminder that can come due? 1: "due:" (any case) and a date YYYY,
 * YYYY-MM or YYYY-MM-DD with a month 01..12 and a day 01..31. 0: it starts
 * with "due:" but the rest is not that shape, a plain key ais_due never
 * matches. -1: not a due key at all. Pure; the day is not checked against the
 * month (due:2026-11-31 passes here and elapses on 12-01, as ais_due has it). */
int due_key_check(const char *key);

#endif /* AIS_DUE_H */
