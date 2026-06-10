/* main.c -- the AIS command-line front end (getopt_long).
 *
 * Grammar (flag-based, so every tag stays reachable):
 *   bare args     KEYS -- the default action is recall (-o = OR, else AND)
 *   -v VALUE      store VALUE under the keys (repeat -v = one multi-link
 *                 record; -v - reads values from stdin, one per line)
 *   -k KEY        an explicit key (for a key that looks like a flag)
 *   -R DIR        store every file under DIR
 *   -i            interactive: ask keys per piped line
 *   --CMD         a command: find add del del-key dump keys stats compact
 *                 init import where serve project doc. Operands are the bare
 *                 args; values come through -v.
 * No bare word is ever a command, so a tag named "doc" or "find" recalls fine.
 *
 * INDEX location precedence: -f DIR > $AIS_INDEX > nearest .ais/ > per-user
 * (see locate.h). The CLI front-end (this file and feed.c) calls die(); the
 * engine modules return codes.
 */
#define _DEFAULT_SOURCE          /* getopt_long */
#define _POSIX_C_SOURCE 200809L  /* strtok_r */
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
#include "serve.h"

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

/* Join the bare positionals argv[from..argc) and any -k keys into BUF as
 * space-separated keys. Returns 0/-1 (too long). */
static int collect_keys(char *const argv[], int from, int argc,
                        const char *const exk[], int nexk, char *buf, size_t bufsz)
{
    size_t used = 0;
    int i;

    buf[0] = '\0';
    for (i = from; i < argc; i++) {
        int n = snprintf(buf + used, bufsz - used, "%s%s", used ? " " : "", argv[i]);
        if (n < 0 || used + (size_t)n >= bufsz)
            return -1;
        used += (size_t)n;
    }
    for (i = 0; i < nexk; i++) {
        int n = snprintf(buf + used, bufsz - used, "%s%s", used ? " " : "", exk[i]);
        if (n < 0 || used + (size_t)n >= bufsz)
            return -1;
        used += (size_t)n;
    }
    return 0;
}

/* ---- default project key (prepended to the keys on every write) --------- */

/* Read the persistent default project (INDEX/project), trimmed, into OUT
 * (OUT is "" if the file is absent or empty). */
static void read_project(const ais *a, char *out, size_t outsz)
{
    char path[AIS_PATH_MAX];
    FILE *fp;
    size_t n;

    out[0] = '\0';
    if (snprintf(path, sizeof(path), "%s/project", a->dir) >= (int)sizeof(path))
        return;
    fp = fopen(path, "r");
    if (fp == NULL)
        return;
    if (fgets(out, (int)outsz, fp) != NULL) {
        n = strlen(out);
        while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' '))
            out[--n] = '\0';
    }
    fclose(fp);
}

/* Write the persistent default project; an empty KEY removes it. 0/-1. */
static int write_project(const ais *a, const char *key)
{
    char path[AIS_PATH_MAX];
    FILE *fp;

    if (snprintf(path, sizeof(path), "%s/project", a->dir) >= (int)sizeof(path))
        return -1;
    if (key == NULL || key[0] == '\0') {
        remove(path);                 /* clear the default */
        return 0;
    }
    fp = fopen(path, "w");
    if (fp == NULL)
        return -1;
    fprintf(fp, "%s\n", key);
    return (fclose(fp) == 0) ? 0 : -1;
}

/* The effective project key for this invocation, by precedence:
 *   -p flag (empty = explicit none) > $AIS_PROJECT > INDEX/project. */
static void resolve_project(const ais *a, const char *flag, int given,
                            char *out, size_t outsz)
{
    const char *env;

    if (given) {                      /* -p given: its value wins ("" = none) */
        snprintf(out, outsz, "%s", flag ? flag : "");
        return;
    }
    env = getenv("AIS_PROJECT");
    if (env != NULL && env[0] != '\0') {
        snprintf(out, outsz, "%s", env);
        return;
    }
    read_project(a, out, outsz);
}

/* OUT = "PROJECT KEYS" (two writes, so no "%s %s" truncation warning). */
static void build_keys(const char *project, const char *keys,
                       char *out, size_t outsz)
{
    size_t k = 0;

    out[0] = '\0';
    if (project[0] != '\0') {
        int n = snprintf(out, outsz, "%s", project);
        k = (n > 0) ? (size_t)n : 0;
    }
    if (keys[0] != '\0' && k < outsz)
        snprintf(out + k, outsz - k, "%s%s", k ? " " : "", keys);
}

/* Print one key per line to stdout (the ais_keys callback). */
static int print_key(const char *key, void *vp)
{
    FILE *out = vp;
    fputs(key, out);
    fputc('\n', out);
    return 0;
}

static void do_get(ais *a, char *const keys[], int nkeys, ais_mode mode)
{
    struct get_ctx g;
    g.a = a;
    g.printed_any = 0;
    if (ais_get(a, keys, nkeys, mode, on_id, &g) < 0)
        die("get failed");
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

/* ---- main --------------------------------------------------------------- */

int main(int argc, char **argv)
{
    enum { OPT_HELP = 1000, OPT_VERSION,
           CMD_FIND, CMD_ADD, CMD_DEL, CMD_DELKEY, CMD_DUMP, CMD_KEYS, CMD_STATS,
           CMD_COMPACT, CMD_INIT, CMD_IMPORT, CMD_WHERE, CMD_SERVE, CMD_PROJECT,
           CMD_DOC };
    static const struct option longopts[] = {
        { "index",       required_argument, NULL, 'f' },
        { "or",          no_argument,       NULL, 'o' },
        { "debug",       no_argument,       NULL, 'd' },
        { "yes",         no_argument,       NULL, 'y' },
        { "interactive", no_argument,       NULL, 'i' },
        { "recurse",     required_argument, NULL, 'R' },
        { "value",       required_argument, NULL, 'v' },
        { "key",         required_argument, NULL, 'k' },
        { "project",     no_argument,       NULL, CMD_PROJECT },
        { "help",        no_argument,       NULL, OPT_HELP },
        { "version",     no_argument,       NULL, OPT_VERSION },
        { "find",        no_argument,       NULL, CMD_FIND },
        { "add",         no_argument,       NULL, CMD_ADD },
        { "del",         no_argument,       NULL, CMD_DEL },
        { "del-key",     no_argument,       NULL, CMD_DELKEY },
        { "dump",        no_argument,       NULL, CMD_DUMP },
        { "keys",        no_argument,       NULL, CMD_KEYS },
        { "stats",       no_argument,       NULL, CMD_STATS },
        { "compact",     no_argument,       NULL, CMD_COMPACT },
        { "init",        no_argument,       NULL, CMD_INIT },
        { "import",      no_argument,       NULL, CMD_IMPORT },
        { "where",       no_argument,       NULL, CMD_WHERE },
        { "serve",       no_argument,       NULL, CMD_SERVE },
        { "doc",         no_argument,       NULL, CMD_DOC },
        { NULL, 0, NULL, 0 }
    };
    const char *dir = NULL;
    const char *project_arg = NULL;
    const char *recurse_dir = NULL;
    const char *values[AIS_KEYS_MAX];
    const char *exkeys[AIS_KEYS_MAX];
    int nval = 0, nexk = 0;
    ais_mode mode = AIS_AND;
    int assume_yes = 0, interactive = 0, project_given = 0;
    int cmd = 0;
    char project[AIS_KEY_MAX];
    char keys[AIS_LINE_MAX], full[AIS_LINE_MAX];
    ais a;
    int c;

    /* -p is the per-call project override; --project (CMD_PROJECT) manages the
     * stored default. They are intentionally distinct. */
    while ((c = getopt_long(argc, argv, "f:odhyiR:v:k:p:", longopts, NULL)) != -1) {
        switch (c) {
        case 'f': dir = optarg; break;
        case 'o': mode = AIS_OR; break;
        case 'd': ais_debug_flag = 1; break;
        case 'y': assume_yes = 1; break;
        case 'i': interactive = 1; break;
        case 'R': recurse_dir = optarg; break;
        case 'v': if (nval >= AIS_KEYS_MAX) die("too many -v values");
                  values[nval++] = optarg; break;
        case 'k': if (nexk >= AIS_KEYS_MAX) die("too many -k keys");
                  exkeys[nexk++] = optarg; break;
        case 'p': project_arg = optarg; project_given = 1; break;
        case 'h': usage_short(stdout); return 0;
        case OPT_HELP:    usage_long(stdout);  return 0;
        case OPT_VERSION: printf("ais %s\n", AIS_VERSION); return 0;
        case CMD_FIND: case CMD_ADD: case CMD_DEL: case CMD_DELKEY:
        case CMD_DUMP: case CMD_KEYS: case CMD_STATS: case CMD_COMPACT:
        case CMD_INIT: case CMD_IMPORT: case CMD_WHERE: case CMD_SERVE:
        case CMD_PROJECT: case CMD_DOC:
            if (cmd != 0) die("only one command at a time");
            cmd = c;
            break;
        default: usage_short(stderr); return 2;
        }
    }

    /* nothing asked for at all */
    if (cmd == 0 && nval == 0 && recurse_dir == NULL && !interactive &&
        optind >= argc && nexk == 0) {
        usage_short(stderr);
        return 2;
    }

    /* resolve the index (--init without -f targets a fresh .ais here) */
    {
        static char resolved[AIS_PATH_MAX];
        if (cmd == CMD_INIT && (dir == NULL || dir[0] == '\0')) {
            snprintf(resolved, sizeof(resolved), ".ais");
            dir = resolved;
        } else if (ais_locate(dir, resolved, sizeof(resolved)) != 0) {
            die("cannot determine an index location (use -f, or set $HOME / $XDG_DATA_HOME)");
        } else {
            dir = resolved;
        }
    }

    if (ais_open(&a, dir) != 0)
        die("cannot open index '%s' (in use, or unwritable)", dir);

    resolve_project(&a, project_arg, project_given, project, sizeof(project));

    /* ---- commands (--CMD); operands are the bare args, values via -v ---- */
    if (cmd != 0) {
        switch (cmd) {
        case CMD_DUMP:  ais_dump(&a, stdout); break;
        case CMD_KEYS:  if (ais_keys(&a, print_key, stdout) < 0) die("keys failed"); break;
        case CMD_STATS: if (ais_stats(&a, stdout) != 0) die("stats failed"); break;
        case CMD_WHERE: printf("%s\n", dir); break;
        case CMD_INIT:  printf("initialized AIS index: %s\n", dir); break;
        case CMD_IMPORT: feed_import(&a); break;
        case CMD_FIND:
            if (optind >= argc) die("--find needs TEXT");
            if (ais_find(&a, argv[optind], stdout) != 0) die("find failed");
            break;
        case CMD_ADD: {
            long id; int j;
            if (optind >= argc) die("--add needs an ID");
            if (nval == 0) die("--add needs at least one -v VALUE");
            id = atol(argv[optind]);
            for (j = 0; j < nval; j++)
                if (ais_add(&a, id, values[j]) != 0)
                    die("--add: no record id %ld", id);
            break;
        }
        case CMD_DEL: {
            long id;
            if (optind >= argc) die("--del needs an ID");
            id = atol(argv[optind]);
            if (!assume_yes) {
                char p[64];
                snprintf(p, sizeof(p), "Delete record %ld?", id);
                if (!confirm(p)) { fprintf(stderr, "aborted\n"); break; }
            }
            if (ais_del(&a, id) != 0) die("del failed");
            break;
        }
        case CMD_DELKEY: {
            const char *key = (optind < argc) ? argv[optind]
                            : (nexk ? exkeys[0] : NULL);
            char p[AIS_KEY_MAX + 48];
            if (key == NULL) die("--del-key needs a KEY");
            snprintf(p, sizeof(p), "Delete every record filed under '%s'?", key);
            if (assume_yes || confirm(p)) {
                long n = ais_del_key(&a, key);
                if (n < 0) die("del-key failed");
                printf("deleted %ld\n", n);
            } else fprintf(stderr, "aborted\n");
            break;
        }
        case CMD_COMPACT:
            if (assume_yes ||
                confirm("Compaction permanently discards deleted records. Proceed?")) {
                if (ais_compact(&a) != 0) die("compact failed");
            } else fprintf(stderr, "aborted\n");
            break;
        case CMD_SERVE: {
            int port = (optind < argc) ? atoi(argv[optind]) : 8765;
            if (port <= 0) port = 8765;
            if (ais_serve(&a, port) != 0)
                die("serve: cannot bind 127.0.0.1:%d", port);
            break;
        }
        case CMD_PROJECT:
            if (optind < argc) {              /* set (empty arg clears it) */
                if (write_project(&a, argv[optind]) != 0) die("project: cannot write");
                if (argv[optind][0] == '\0') printf("default project cleared\n");
                else printf("default project: %s\n", argv[optind]);
            } else {                          /* show */
                char cur[AIS_KEY_MAX];
                read_project(&a, cur, sizeof(cur));
                printf("%s\n", cur[0] != '\0' ? cur : "(no default project)");
            }
            break;
        case CMD_DOC:
            if (collect_keys(argv, optind, argc, exkeys, nexk, keys, sizeof(keys)) != 0)
                die("key list too long");
            build_keys(project, keys, full, sizeof(full));
            feed_doc(&a, full);
            break;
        }
        ais_close(&a);
        return 0;
    }

    /* ---- store (put mode): -v, -R, or -i ---- */
    if (nval > 0 || recurse_dir != NULL || interactive) {
        if (collect_keys(argv, optind, argc, exkeys, nexk, keys, sizeof(keys)) != 0)
            die("key list too long");
        build_keys(project, keys, full, sizeof(full));

        if (recurse_dir != NULL) {
            feed_dir(&a, recurse_dir, full);
        } else if (interactive) {
            feed_interactive(&a, full);
        } else if (nval == 1 && strcmp(values[0], "-") == 0) {
            feed_stdin(&a, full);                 /* values from stdin */
        } else {
            long id = ais_put(&a, full, values[0]);
            int j;
            if (id < 0) die("put failed");
            for (j = 1; j < nval; j++)            /* extra -v = multi-link */
                if (ais_add(&a, id, values[j]) != 0) die("add failed");
            printf("%ld\n", id);
        }
        ais_close(&a);
        return 0;
    }

    /* ---- recall (default): bare keys + -k, project NOT prepended ---- */
    if (collect_keys(argv, optind, argc, exkeys, nexk, keys, sizeof(keys)) != 0)
        die("key list too long");
    {
        char *kv[AIS_KEYS_MAX];
        int nk = 0;
        char *tok, *save;
        for (tok = strtok_r(keys, " ", &save); tok != NULL && nk < AIS_KEYS_MAX;
             tok = strtok_r(NULL, " ", &save))
            kv[nk++] = tok;
        if (nk == 0) { usage_short(stderr); ais_close(&a); return 2; }
        do_get(&a, kv, nk, mode);
    }

    ais_close(&a);
    return 0;
}

#endif /* UNIT_TEST */
