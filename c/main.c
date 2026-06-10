/* main.c -- the AIS command-line front end (getopt_long).
 *
 * get is the default verb: bare positional args are query keys (AND; -o = OR).
 * Mutations are explicit verbs: put, add, del, dump, compact. Dispatch is
 * verb-first: if argv after options begins with a known verb, run it; else
 * treat all positionals as get keys.
 *
 * INDEX location precedence: -f/--index DIR > $AIS_INDEX > ./INDEX.
 * The CLI front-end (this file and feed.c) calls die(); the engine modules
 * return codes.
 */
#define _DEFAULT_SOURCE      /* getopt_long */
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ais.h"
#include "help.h"
#include "log.h"
#include "feed.h"
#include "stats.h"
#include "find.h"
#include "locate.h"

#define AIS_VERSION "0.1"

#ifndef UNIT_TEST

/* ---- get / record output ------------------------------------------------ */

/* For each surviving id, print its record's values (one line per link),
 * "id|value". A keyed record with no link yet prints "id|". */
struct get_ctx {
    ais *a;
    int  printed_any;   /* per-id: did ais_record emit at least one value?   */
};

static int print_value(long id, const char *value, void *vp)
{
    struct get_ctx *g = vp;
    printf("%ld|%s\n", id, value);
    g->printed_any = 1;
    return 0;
}

static int on_id(long id, void *vp)
{
    struct get_ctx *g = vp;
    g->printed_any = 0;
    ais_record(g->a, id, print_value, g);
    if (!g->printed_any)
        printf("%ld|\n", id);   /* keyed but value-less */
    return 0;
}

/* Join argv[from..argc) into BUF as space-separated keys. Returns 0/-1. */
static int join_keys(char *const argv[], int from, int argc,
                     char *buf, size_t bufsz)
{
    size_t used = 0;
    int i;

    buf[0] = '\0';
    for (i = from; i < argc; i++) {
        int n = snprintf(buf + used, bufsz - used, "%s%s",
                         (i > from) ? " " : "", argv[i]);
        if (n < 0 || used + (size_t)n >= bufsz)
            return -1;
        used += (size_t)n;
    }
    return 0;
}

/* ---- keys --------------------------------------------------------------- */

/* Print one key per line to stdout (the ais_keys callback). */
static int print_key(const char *key, void *vp)
{
    FILE *out = vp;
    fputs(key, out);
    fputc('\n', out);
    return 0;
}

/* ---- verbs -------------------------------------------------------------- */

static int is_verb(const char *s)
{
    return strcmp(s, "put") == 0 || strcmp(s, "add") == 0 ||
           strcmp(s, "del") == 0 || strcmp(s, "get") == 0 ||
           strcmp(s, "dump") == 0 || strcmp(s, "compact") == 0 ||
           strcmp(s, "keys") == 0 ||
           strcmp(s, "stats") == 0 || strcmp(s, "del-key") == 0 ||
           strcmp(s, "find") == 0 || strcmp(s, "init") == 0;
}

static void do_get(ais *a, char *const keys[], int nkeys, ais_mode mode)
{
    struct get_ctx g;
    g.a = a;
    g.printed_any = 0;
    if (ais_get(a, keys, nkeys, mode, on_id, &g) < 0)
        die("get failed");
}

static void do_put(ais *a, int argc, char **argv, int idx)
{
    char keys[AIS_LINE_MAX];

    /* put -R DIR KEY... : index every file under DIR (recursive) */
    if (idx < argc && strcmp(argv[idx], "-R") == 0) {
        if (idx + 1 >= argc)
            die("put -R needs a directory");
        if (idx + 2 >= argc)
            die("put -R needs at least one key");
        if (join_keys(argv, idx + 2, argc, keys, sizeof(keys)) != 0)
            die("key list too long");
        feed_dir(a, argv[idx + 1], keys);
        return;
    }

    /* put - KEY... : file each stdin line (a value) under KEY... */
    if (idx < argc && strcmp(argv[idx], "-") == 0) {
        if (idx + 1 >= argc)
            die("put - needs at least one key");
        if (join_keys(argv, idx + 1, argc, keys, sizeof(keys)) != 0)
            die("key list too long");
        feed_stdin(a, keys);
        return;
    }

    /* put VALUE KEY... : a single value */
    if (idx >= argc)
        die("put needs a VALUE and at least one KEY");
    if (idx + 1 >= argc)
        die("put needs at least one KEY after the value");
    if (join_keys(argv, idx + 1, argc, keys, sizeof(keys)) != 0)
        die("key list too long");
    {
        long id = ais_put(a, keys, argv[idx]);
        if (id < 0)
            die("put failed");
        printf("%ld\n", id);
    }
}

static void do_add(ais *a, int argc, char **argv, int idx)
{
    long id;
    if (idx + 1 >= argc)
        die("add needs an ID and a VALUE");
    id = atol(argv[idx]);
    if (ais_add(a, id, argv[idx + 1]) != 0)
        die("add: no record id %ld", id);
}

/* Confirm a destructive action: read a line from stdin, return 1 only on y/yes.
 * AIS is append-only by default, so removing anything needs an explicit yes. */
static int confirm(const char *prompt)
{
    char buf[16];
    fprintf(stderr, "%s [y/N] ", prompt);
    fflush(stderr);
    if (fgets(buf, sizeof(buf), stdin) == NULL)
        return 0;                          /* EOF / no terminal -> No */
    return buf[0] == 'y' || buf[0] == 'Y';
}

static void do_del(ais *a, int argc, char **argv, int idx, int assume_yes)
{
    long id;
    if (idx >= argc)
        die("del needs an ID");
    id = atol(argv[idx]);
    if (!assume_yes) {
        char p[64];
        snprintf(p, sizeof(p), "Delete record %ld?", id);
        if (!confirm(p)) {
            fprintf(stderr, "aborted\n");
            return;
        }
    }
    if (ais_del(a, id) != 0)
        die("del failed");
}

/* ---- main --------------------------------------------------------------- */

int main(int argc, char **argv)
{
    static const struct option longopts[] = {
        { "index", required_argument, NULL, 'f' },
        { "or",    no_argument,       NULL, 'o' },
        { "debug", no_argument,       NULL, 'd' },
        { "help",    no_argument,     NULL, 'H' },   /* long-only: full help */
        { "version", no_argument,     NULL, 'V' },
        { "yes",         no_argument,  NULL, 'y' },
        { "interactive", no_argument,  NULL, 'i' },
        { NULL,      0,               NULL,  0  }
    };
    const char *dir = NULL;
    ais_mode mode = AIS_AND;
    int assume_yes = 0;
    int interactive = 0;
    ais a;
    int c, idx;

    /* '+': stop option parsing at the first non-option, so a value or key that
     * looks like an option (rare) is not eaten; verbs/keys follow. */
    while ((c = getopt_long(argc, argv, "+f:odhVyi", longopts, NULL)) != -1) {
        switch (c) {
        case 'f': dir = optarg;             break;
        case 'o': mode = AIS_OR;            break;
        case 'i': interactive = 1;          break;
        case 'y': assume_yes = 1;           break;
        case 'd': ais_debug_flag = 1;       break;
        case 'h': usage_short(stdout);      return 0;
        case 'H': usage_long(stdout);       return 0;
        case 'V': printf("ais %s\n", AIS_VERSION); return 0;
        default:  usage_short(stderr);      return 2;
        }
    }

    if (optind >= argc && !interactive) {
        usage_short(stderr);
        return 2;
    }

    /* Resolve the index location. `init` (without -f) targets a fresh .ais
     * here; otherwise follow the precedence in locate.h. */
    {
        static char resolved[AIS_PATH_MAX];
        if (!interactive && strcmp(argv[optind], "init") == 0 &&
            (dir == NULL || dir[0] == '\0')) {
            snprintf(resolved, sizeof(resolved), ".ais");   /* a local index here */
            dir = resolved;
        } else if (ais_locate(dir, resolved, sizeof(resolved)) != 0) {
            die("cannot determine an index location (use -f, or set $HOME / $XDG_DATA_HOME)");
        } else {
            dir = resolved;
        }
    }

    if (ais_open(&a, dir) != 0)
        die("cannot open index '%s' (in use, or unwritable)", dir);

    if (interactive) {
        char base[AIS_LINE_MAX];
        if (join_keys(argv, optind, argc, base, sizeof(base)) != 0)
            die("key list too long");
        feed_interactive(&a, base);
        ais_close(&a);
        return 0;
    }

    idx = optind;
    if (is_verb(argv[idx])) {
        const char *verb = argv[idx++];
        if (strcmp(verb, "put") == 0)
            do_put(&a, argc, argv, idx);
        else if (strcmp(verb, "add") == 0)
            do_add(&a, argc, argv, idx);
        else if (strcmp(verb, "del") == 0)
            do_del(&a, argc, argv, idx, assume_yes);
        else if (strcmp(verb, "dump") == 0)
            ais_dump(&a, stdout);
        else if (strcmp(verb, "keys") == 0) {
            if (ais_keys(&a, print_key, stdout) < 0)
                die("keys failed");
        }
        else if (strcmp(verb, "compact") == 0) {
            if (assume_yes ||
                confirm("Compaction permanently discards deleted records. Proceed?")) {
                if (ais_compact(&a) != 0)
                    die("compact failed");
            } else {
                fprintf(stderr, "aborted\n");
            }
        }
        else if (strcmp(verb, "stats") == 0) {
            if (ais_stats(&a, stdout) != 0)
                die("stats failed");
        }
        else if (strcmp(verb, "find") == 0) {
            if (idx >= argc)
                die("find needs TEXT");
            if (ais_find(&a, argv[idx], stdout) != 0)
                die("find failed");
        }
        else if (strcmp(verb, "init") == 0) {
            /* ais_open already created/validated the dir; just confirm it. */
            printf("initialized AIS index: %s\n", dir);
        }
        else if (strcmp(verb, "del-key") == 0) {
            if (idx >= argc)
                die("del-key needs a KEY");
            {
                char p[AIS_KEY_MAX + 48];
                snprintf(p, sizeof(p),
                         "Delete every record filed under '%s'?", argv[idx]);
                if (assume_yes || confirm(p)) {
                    long n = ais_del_key(&a, argv[idx]);
                    if (n < 0)
                        die("del-key failed");
                    printf("deleted %ld\n", n);
                } else {
                    fprintf(stderr, "aborted\n");
                }
            }
        } else   /* "get" */
            do_get(&a, &argv[idx], argc - idx, mode);
    } else {
        do_get(&a, &argv[idx], argc - idx, mode);
    }

    ais_close(&a);
    return 0;
}

#endif /* UNIT_TEST */
