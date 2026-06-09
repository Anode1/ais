/* log.h -- CLI fatal error and runtime-gated trace.
 *
 * die() is for main.c only: print to stderr and exit non-zero. Modules never
 * call it -- they return -1 and let the caller decide (see doc/STYLE.md).
 * debug() prints to stderr only when the runtime debug flag is set (-d).
 */
#ifndef AIS_LOG_H
#define AIS_LOG_H

/* Runtime trace flag, set from main's -d. 0 = quiet (default). */
extern int ais_debug_flag;

/* Print "ais: " + formatted message to stderr, then exit(EXIT_FAILURE).
 * CLI use only. Does not return. */
void die(const char *fmt, ...);

/* Print formatted message to stderr only when ais_debug_flag is nonzero. */
void debug(const char *fmt, ...);

#endif /* AIS_LOG_H */
