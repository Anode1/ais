/* main.c -- the AIS command-line front end (getopt_long).
 *
 * Grammar (flag-based, so every tag stays reachable):
 *   bare args     KEYS -- the default action is recall (-o = OR, else AND)
 *   -v VALUE      store VALUE under the keys (repeat -v = one multi-link
 *                 record; -v - reads values from stdin, one per line)
 *   -k KEY        an explicit key (for a key that looks like a flag)
 *   -i            interactive: ask keys per piped line
 *   --CMD         a command: find add del del-key dump keys tags timeline
 *                 stats compact init import where serve project doc. Operands
 *                 are the bare args; values come through -v.
 * No bare word is ever a command, so a tag named "doc" or "find" recalls fine.
 *
 * INDEX location precedence: -f DIR > nearest .ais/ > saved default in
 * ~/.ais/config > the built-in ~/.ais (see locate.h). No env vars -- the index
 * comes from argv. The CLI front-end (this file and feed.c) calls die(); the
 * engine modules return codes.
 */
#define _DEFAULT_SOURCE          /* getopt_long */
#define _POSIX_C_SOURCE 200809L  /* strtok_r */
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "ais.h"
#include "compact.h"   /* tomb_contains: do not offer to delete a deleted record */
#include "doc.h"           /* ais_doc_is_blob: recall cats a document, not its path */
#include "help.h"
#include "log.h"
#include "feed.h"
#include "stats.h"
#include "find.h"
#include "locate.h"
#include "serve.h"
#include "sync.h"
#include "secret.h"

/* Stamped from the git tag at build time (-DAIS_VERSION, see c/Makefile, single
 * source of truth). This default applies only to a build with no git/tag (e.g. a
 * bare source copy). */
#ifndef AIS_VERSION
#define AIS_VERSION "0.0.0-dev"
#endif

#ifndef UNIT_TEST

/* ---- get / record output ------------------------------------------------ */

/* For each surviving id, print its record's values (one line per link),
 * "id|value". A keyed record with no link yet prints "id|". */
struct get_ctx {
    ais *a;
    int  printed_any;   /* per-id: did ais_record emit at least one value?   */
    int  reveal;        /* interactive recall at a tty: reveal "aisc:" secrets */
};

static int print_value(long id, const char *value, void *vp)
{
    struct get_ctx *g = vp;
    char path[AIS_PATH_MAX];
    FILE *f;
    if (g->reveal && secret_is_marked(value)) {
        secret_reveal(id, value, g->a->dir);   /* decrypt-dialog to /dev/tty, not stdout */
    } else if (ais_doc_is_blob(g->a, value, path, sizeof path)
               && (f = fopen(path, "rb")) != NULL) {
        char buf[8192];                         /* a document blob: cat its CONTENT, not the path */
        size_t r;
        int last = '\n';
        printf("%ld|", id);
        while ((r = fread(buf, 1, sizeof buf, f)) > 0) {
            fwrite(buf, 1, r, stdout);
            last = (unsigned char)buf[r - 1];
        }
        fclose(f);
        if (last != '\n')
            putchar('\n');                      /* keep records line-separated */
    } else {
        printf("%ld|%s\n", id, value);     /* opaque: a normal value, or an "aisc:" blob */
    }
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

/* Before a record is tombstoned, shred any encrypted-blob payload it holds so
 * the ciphertext file does not outlive the record. VP is the index dir. */
static int shred_value_cb(long id, const char *value, void *vp)
{
    (void)id;
    secret_shred_blob((const char *)vp, value);
    return 0;
}

/* del-key preview: count the records a key would take with it. VP is a long *. */
static int delkey_count_cb(long id, void *vp)
{
    (void)id;
    (*(long *)vp)++;
    return 0;
}

/* One preview line per record, on STDERR. Deliberately NOT the recall printer:
 *  - recall writes to stdout, so `ais --del-key k > file` showed the user an
 *    EMPTY kill-list above the prompt -- the most dangerous state this can be in;
 *  - recall prints every link of a multi-link record and cats a --doc blob whole,
 *    burying the record boundaries;
 *  - recall reveals secrets, so previewing a key holding encrypted records asked
 *    for passphrases in the middle of a delete confirmation.
 * Shows the FIRST value only, truncated, and never decrypts. */
#define DELKEY_PREVIEW_MAX 10
#define DELKEY_VALUE_WIDTH 68
struct delkey_preview { ais *a; long shown; };

/* Prints the record's FIRST value, then keeps counting the rest: a record can
 * hold several links and showing only one made the manifest UNDERSTATE what the
 * delete takes -- the user consented to one line and lost four. Only the first is
 * printed (a --doc blob or a ten-link record must not bury the record boundaries);
 * the others are summarised as a count. VP is a long * (links seen). */
static int delkey_line_cb(long id, const char *value, void *vp)
{
    long *nlink = vp;

    if ((*nlink)++ == 0)
        fprintf(stderr, "  %ld|%.*s%s\n", id, DELKEY_VALUE_WIDTH, value,
                strlen(value) > (size_t)DELKEY_VALUE_WIDTH ? "..." : "");
    return 0;
}

/* Print one manifest entry. Also THE counter: a separate ais_get to count first
 * walked the whole posting twice, and the two walks could disagree. */
static int delkey_show_cb(long id, void *vp)
{
    struct delkey_preview *p = vp;

    if (p->shown < DELKEY_PREVIEW_MAX) {
        long nlink = 0;
        ais_record(p->a, id, delkey_line_cb, &nlink);
        if (nlink > 1)
            fprintf(stderr, "     (+%ld more link%s on this record)\n",
                    nlink - 1, nlink == 2 ? "" : "s");
    }
    p->shown++;
    return 0;
}

/* del-key pre-pass: for each matched id, shred its encrypted blobs. VP is the
 * ais handle (its ->dir is where blobs live). */
static int shred_id_cb(long id, void *vp)
{
    ais *a = vp;
    ais_record(a, id, shred_value_cb, a->dir);
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
 *   -p flag (empty = explicit none) > the stored default (set via ais --project). */
static void resolve_project(const ais *a, const char *flag, int given,
                            char *out, size_t outsz)
{
    if (given) {                      /* -p given: its value wins ("" = none) */
        snprintf(out, outsz, "%s", flag ? flag : "");
        return;
    }
    read_project(a, out, outsz);      /* else the stored default */
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

/* Print one tag as "<count>  <key>" (the ais_tags callback), busiest first. */
static int print_tag(const char *key, long count, void *vp)
{
    FILE *out = vp;
    fprintf(out, "%6ld  %s\n", count, key);
    return 0;
}

/* One timeline row, tab-separated for awk/grep: "WHEN\tKEYS\tVALUE". WHEN is
 * "YYYY-MM-DD HH:MM" (the engine writes seconds; we show to the minute) or the
 * literal "(undated)" so a dateless record is visible, not hidden. */
static int print_tl(long id, const char *ts, const char *keys,
                    const char *value, void *vp)
{
    FILE *out = vp;
    char when[24];
    char *t;

    (void)id;
    if (ts[0] == '\0')
        snprintf(when, sizeof(when), "(undated)");
    else {
        snprintf(when, sizeof(when), "%.16s", ts);   /* date + hh:mm */
        t = strchr(when, 'T');
        if (t != NULL)
            *t = ' ';
        if (strchr(ts, 'Z') != NULL)                 /* keep the UTC marker */
            strncat(when, "Z", sizeof(when) - strlen(when) - 1);
    }
    fprintf(out, "%s\t%s\t%s\n", when, keys[0] ? keys : "(no keys)", value);
    return 0;
}

/* One registered index as "<*| > name\tpath" for --indexes; '*' = the current. */
struct index_ctx { const char *cur; };
static int print_index(const char *name, const char *path, void *vp)
{
    struct index_ctx *ic = vp;
    printf("%c %s\t%s\n",
           (ic->cur[0] != '\0' && strcmp(name, ic->cur) == 0) ? '*' : ' ', name, path);
    return 0;
}

static void do_get(ais *a, char *const keys[], int nkeys, ais_mode mode)
{
    struct get_ctx g;
    g.a = a;
    g.printed_any = 0;
    g.reveal = secret_reveal_context();    /* only an interactive recall reveals secrets */
    if (ais_get(a, keys, nkeys, mode, on_id, &g) < 0)
        die("get failed");
}

/* Confirm a destructive action: read a line from stdin, return 1 only on y/yes.
 * AIS is append-only by default, so removing anything needs an explicit yes. */
static int confirm(const char *prompt)
{
    const char *ttypath = getenv("AIS_TTY");   /* a file overrides the terminal */
    char buf[16];
    FILE *tty;
    size_t n;

    /* Read the answer from the TERMINAL, not stdin -- the same seam
     * feed_import_interactive uses. Reading stdin let a redirected data file
     * answer a destructive prompt: `ais --del-key kul < notes.txt` was confirmed
     * by whatever the first line happened to start with. */
#ifdef _WIN32
    tty = fopen(ttypath != NULL ? ttypath : "CONIN$", "r");
#else
    tty = fopen(ttypath != NULL ? ttypath : "/dev/tty", "r");
#endif
    if (tty == NULL) {
        /* No terminal to ask on. Aborting quietly with status 0 would tell a
         * script the work was done, so say what is missing and fail. */
        fprintf(stderr, "no terminal to confirm on -- pass -y (or set AIS_TTY=FILE)\n");
        exit(2);
    }
    if (ttypath != NULL)
        /* The seam must never be SILENT. A destructive command run with AIS_TTY
         * left set in the environment would otherwise be confirmed by a file with
         * no sign on screen that nobody was asked. */
        fprintf(stderr, "ais: reading the answer from AIS_TTY=%s, not a terminal\n",
                ttypath);
    fprintf(stderr, "%s [y/N] ", prompt);
    fflush(stderr);
    if (fgets(buf, sizeof(buf), tty) == NULL) {
        fclose(tty);
        fprintf(stderr, "\nno answer -- pass -y to confirm non-interactively\n");
        exit(2);
    }
    fclose(tty);
    n = strcspn(buf, "\r\n");
    buf[n] = '\0';
    /* Exactly "y" or "yes". A prefix test accepted "yolo" as consent. */
    return (strcmp(buf, "y") == 0 || strcmp(buf, "Y") == 0 ||
            strcasecmp(buf, "yes") == 0);
}

/* ---- main --------------------------------------------------------------- */

int main(int argc, char **argv)
{
    enum { OPT_HELP = 1000, OPT_VERSION, OPT_TOKEN, OPT_PURGE,
           CMD_FIND, CMD_ADD, CMD_DEL, CMD_DELKEY, CMD_DUMP, CMD_KEYS, CMD_STATS,
           CMD_COMPACT, CMD_INIT, CMD_IMPORT, CMD_IMPORTI, CMD_WHERE, CMD_SERVE, CMD_PROJECT,
           CMD_DOC, CMD_TIMELINE, CMD_TAGS, CMD_DEFAULT, CMD_UPDATE, CMD_SET, CMD_UNTAG,
           CMD_SWITCH, CMD_INDEXES, CMD_FORGET, CMD_EXPORT, CMD_SYNC, CMD_SYNCFOLDER };
    static const struct option longopts[] = {
        { "index",       required_argument, NULL, 'f' },
        { "or",          no_argument,       NULL, 'o' },
        { "debug",       no_argument,       NULL, 'd' },
        { "yes",         no_argument,       NULL, 'y' },
        { "interactive", no_argument,       NULL, 'i' },
        { "encrypt",     no_argument,       NULL, 'e' },
        { "value",       required_argument, NULL, 'v' },
        { "key",         required_argument, NULL, 'k' },
        { "project",     no_argument,       NULL, CMD_PROJECT },
        { "default",     no_argument,       NULL, CMD_DEFAULT },
        { "switch",      no_argument,       NULL, CMD_SWITCH },
        { "indexes",     no_argument,       NULL, CMD_INDEXES },
        { "forget",      no_argument,       NULL, CMD_FORGET },
        { "help",        no_argument,       NULL, OPT_HELP },
        { "version",     no_argument,       NULL, OPT_VERSION },
        { "find",        no_argument,       NULL, CMD_FIND },
        { "add",         no_argument,       NULL, CMD_ADD },
        { "update",      no_argument,       NULL, CMD_UPDATE },
        { "set",         no_argument,       NULL, CMD_SET },
        { "del",         no_argument,       NULL, CMD_DEL },
        { "del-key",     no_argument,       NULL, CMD_DELKEY },   /* alias, kept forever */
        { "del-under",   no_argument,       NULL, CMD_DELKEY },
        { "untag",       no_argument,       NULL, CMD_UNTAG },
        { "dump",        no_argument,       NULL, CMD_DUMP },
        { "keys",        no_argument,       NULL, CMD_KEYS },
        { "tags",        no_argument,       NULL, CMD_TAGS },
        { "timeline",    no_argument,       NULL, CMD_TIMELINE },
        { "stats",       no_argument,       NULL, CMD_STATS },
        { "compact",     no_argument,       NULL, CMD_COMPACT },
        { "forget-deleted", no_argument,    NULL, OPT_PURGE },
        { "init",        no_argument,       NULL, CMD_INIT },
        { "import",      no_argument,       NULL, CMD_IMPORT },
        { "import-interactively", no_argument, NULL, CMD_IMPORTI },
        { "export",      no_argument,       NULL, CMD_EXPORT },
        { "sync",        no_argument,       NULL, CMD_SYNC },
        { "sync-folder", no_argument,       NULL, CMD_SYNCFOLDER },
        { "where",       no_argument,       NULL, CMD_WHERE },
        { "serve",       no_argument,       NULL, CMD_SERVE },
        { "token",       required_argument, NULL, OPT_TOKEN },
        { "doc",         no_argument,       NULL, CMD_DOC },
        { NULL, 0, NULL, 0 }
    };
    const char *dir = NULL;
    const char *project_arg = NULL;
    const char *values[AIS_KEYS_MAX];
    const char *exkeys[AIS_KEYS_MAX];
    int nval = 0, nexk = 0;
    ais_mode mode = AIS_AND;
    int assume_yes = 0, interactive = 0, project_given = 0, create = 0, encrypt = 0;
    int cmd = 0, serve_flag = 0;
    /* Which long option spelled the command. Scanning argv for "--del-key" both
     * misfired on a KEY literally named that and missed getopt's own unambiguous
     * abbreviation ("--del-k"), so take the answer from getopt itself. */
    const char *cmd_spelling = "";
    int purge_deletes = 0;             /* --forget-deleted, a modifier on --compact */
    int li = -1;
    const char *token_arg = NULL;
    char project[AIS_KEY_MAX];
    char keys[AIS_LINE_MAX], full[AIS_LINE_MAX];
    ais a;
    int c;

    /* -p is the per-call project override; --project (CMD_PROJECT) manages the
     * stored default. They are intentionally distinct. */
    while (li = -1, (c = getopt_long(argc, argv, "f:odhyiv:k:p:ce", longopts, &li)) != -1) {
        if (c >= OPT_HELP && li >= 0)          /* every long option code is >= OPT_HELP */
            cmd_spelling = longopts[li].name;
        switch (c) {
        case 'f': dir = optarg; break;
        case 'o': mode = AIS_OR; break;
        case 'd': ais_debug_flag = 1; break;
        case 'y': assume_yes = 1; break;
        case OPT_PURGE: purge_deletes = 1; break;
        case 'i': interactive = 1; break;
        case 'c': create = 1; break;
        case 'e': encrypt = 1; break;
        case 'v': if (nval >= AIS_KEYS_MAX) die("too many -v values");
                  values[nval++] = optarg; break;
        case 'k': if (nexk >= AIS_KEYS_MAX) die("too many -k keys");
                  exkeys[nexk++] = optarg; break;
        case 'p': project_arg = optarg; project_given = 1; break;
        case 'h': usage_short(stdout); return 0;
        case OPT_HELP:    usage_long(stdout);  return 0;
        case OPT_VERSION: printf("ais %s\n", AIS_VERSION); return 0;
        case OPT_TOKEN:   token_arg = optarg; break;
        case CMD_SERVE:   serve_flag = 1; break;
        case CMD_FIND: case CMD_ADD: case CMD_DEL: case CMD_DELKEY:
        case CMD_DUMP: case CMD_KEYS: case CMD_STATS: case CMD_COMPACT:
        case CMD_INIT: case CMD_IMPORT: case CMD_IMPORTI: case CMD_EXPORT: case CMD_WHERE:
        case CMD_PROJECT: case CMD_DOC: case CMD_TIMELINE: case CMD_TAGS:
        case CMD_DEFAULT: case CMD_UPDATE: case CMD_SET: case CMD_UNTAG:
        case CMD_SWITCH: case CMD_INDEXES: case CMD_FORGET: case CMD_SYNC:
        case CMD_SYNCFOLDER:
            if (cmd != 0) die("only one command at a time");
            cmd = c;
            break;
        default: usage_short(stderr); return 2;
        }
    }

    /* --serve is a command on its own (web GUI) and also a modifier of --export
     * (serve the merge stream over the LAN). Resolve the combination here. */
    if (serve_flag) {
        if (cmd == 0) cmd = CMD_SERVE;            /* --serve alone -> web GUI */
        else if (cmd != CMD_EXPORT && cmd != CMD_SYNC)
            die("--serve combines only with --export or --sync (or alone for the web GUI)");
    }
    if (token_arg != NULL && !((cmd == CMD_IMPORT || cmd == CMD_SYNC) && optind < argc))
        die("--token is used with: ais --import <url> --token T  (or ais --sync <url> --token T)");

    /* nothing asked for at all */
    if (cmd == 0 && nval == 0 && !interactive && !encrypt &&
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
            die("cannot determine an index location (use -f DIR)");
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
        case CMD_TAGS:  if (ais_tags(&a, print_tag, stdout) < 0) die("tags failed"); break;
        case CMD_TIMELINE: {
            int lim = (optind < argc) ? atoi(argv[optind]) : 0;   /* optional N */
            if (ais_timeline(&a, 0, lim, "", "", print_tl, stdout) < 0) die("timeline failed");
            break;
        }
        case CMD_STATS: if (ais_stats(&a, stdout) != 0) die("stats failed"); break;
        case CMD_WHERE: printf("%s\n", dir); break;
        case CMD_INIT:  printf("initialized AIS index: %s\n", dir); break;
        case CMD_IMPORT:
            if (optind < argc) {                  /* ais --import <url> --token T : pull over LAN */
                if (sync_pull_url(&a, argv[optind], token_arg, 120, 0) != 0) { ais_close(&a); return 1; }
            } else feed_import(&a);               /* ais --import : merge stdin */
            break;
        case CMD_IMPORTI: feed_import_interactive(&a); break;
        case CMD_EXPORT:
            if (serve_flag) {                     /* ais --export --serve [PORT] : serve over LAN */
                int port = (optind < argc) ? atoi(argv[optind]) : AIS_SYNC_PORT;
                if (sync_serve_lan(&a, port, 120, 0) != 0) { ais_close(&a); return 1; }
            } else feed_export(&a, stdout);       /* ais --export : merge stream to stdout */
            break;
        case CMD_SYNC:                            /* symmetric: both sides converge in one round */
            if (serve_flag) {                     /* ais --sync --serve [PORT] : host */
                int port = (optind < argc) ? atoi(argv[optind]) : AIS_SYNC_PORT;
                if (sync_serve_lan(&a, port, 120, 1) != 0) { ais_close(&a); return 1; }
            } else if (optind < argc) {           /* ais --sync <url> --token T : join */
                if (sync_pull_url(&a, argv[optind], token_arg, 120, 1) != 0) { ais_close(&a); return 1; }
            } else {
                die("usage: ais --sync --serve [PORT]  |  ais --sync <url> --token TOKEN");
            }
            break;
        case CMD_SYNCFOLDER:                      /* one export+import pass over a shared folder */
            if (optind >= argc)
                die("usage: ais --sync-folder FOLDER  (a folder a mover like Syncthing keeps in sync)");
            {
                int frc = sync_folder_once_force(&a, argv[optind], assume_yes);
                int err = errno;
                if (frc != 0)
                    ais_close(&a);
                switch (frc) {
                case AIS_FOLDER_MISSING:
                    die("no such folder: %s\n"
                        "       create it first, or check the drive is plugged in.\n"
                        "       (ais will not create it: a typo would look like a\n"
                        "        working backup that never receives anything)",
                        argv[optind]);
                    break;
                case AIS_FOLDER_NOTDIR:
                    die("not a folder: %s", argv[optind]);
                    break;
                case AIS_FOLDER_STAT:
                    die("cannot read folder: %s: %s", argv[optind], strerror(err));
                    break;
                case AIS_FOLDER_STRANGER:
                    die("no device bundles in: %s\n"
                        "       we have synced with that folder before and it is empty now,\n"
                        "       so the drive may not be mounted, or it was emptied or replaced.\n"
                        "       check it, then pass -y to sync with it as it is now.",
                        argv[optind]);
                    break;
                case AIS_FOLDER_NOWRITE:
                    die("cannot write into: %s\n"
                        "       anything the folder had to offer was merged, but this\n"
                        "       device's own bundle could not be written -- the drive may be\n"
                        "       read-only or full, so the other devices will not see this one.",
                        argv[optind]);
                    break;
                case 0: break;
                default: die("folder sync failed"); break;
                }
            }
            printf("synced folder: %s\n", argv[optind]);
            break;
        case CMD_FIND:
            if (optind >= argc) die("--find needs TEXT");
            if (ais_find(&a, argv[optind], stdout) != 0) die("find failed");
            break;
        case CMD_ADD: {
            long id; int j;
            if (optind >= argc) die("--add needs an ID");
            if (nval == 0) die("--add needs at least one -v VALUE");
            id = atol(argv[optind]);
            for (j = 0; j < nval; j++) {
                int arc = ais_add(&a, id, values[j]);
                if (arc == -2)
                    die("--add: another record already holds '%s'\n"
                        "       a value names one record here, so it cannot be on two",
                        values[j]);
                if (arc != 0)
                    die("--add: no record id %ld", id);
            }
            break;
        }
        /* Replace ONE of a record's values in place. The counterpart to --add,
         * which could attach a link but leave no way to correct it: a wrong link
         * could only be removed by deleting the whole record, and a local re-add
         * of any matching value resurrects it (ais_put_at, last-write-wins), so
         * delete-and-recreate silently restores what it just removed. --set edits
         * the one line instead, keeping the id, ts and keys. */
        case CMD_SET: {
            long id;
            if (optind >= argc) die("--set needs an ID");
            if (nval != 2) die("--set needs -v OLD_VALUE -v NEW_VALUE");
            id = atol(argv[optind]);
            /* ais_set_value fails for several reasons and prints its own message for
             * the ones it can explain (a multi-line value), so word this as the
             * likely cause rather than asserting one and contradicting it. */
            if (ais_set_value(&a, id, values[0], values[1]) != 0)
                die("--set: record %ld unchanged (no such id, or no value '%s')", id, values[0]);
            /* The replaced value may have been the ONLY reference to an encrypted
             * blob. Shred it once the store no longer points at it, matching the
             * promise --del/--del-key make; secret_shred_blob is a no-op for any
             * value that is not an aisc: blob. Ordered AFTER the edit so a refused
             * --set never destroys a payload the record still holds. */
            secret_shred_blob(a.dir, values[0]);
            /* An in-place value edit has NO representation in the merge stream:
             * it emits A| for the new value and nothing retiring the old, so a
             * synced peer keeps the old one and feeds it back, leaving BOTH on
             * both devices. Say so rather than let the correction silently undo
             * itself. Only warn on an index that actually syncs. */
            {
                char sp[AIS_PATH_MAX];
                FILE *sf;
                int peered = 0;
                if (snprintf(sp, sizeof sp, "%s/syncid", a.dir) < (int)sizeof sp &&
                    (sf = fopen(sp, "r")) != NULL) { fclose(sf); peered = 1; }
                /* syncid is the FOLDER protocol's identity and a LAN round never
                 * writes it, so keying only on it made this warning invisible to
                 * exactly the users most likely to need it. */
                if (!peered &&
                    snprintf(sp, sizeof sp, "%s/synced", a.dir) < (int)sizeof sp &&
                    (sf = fopen(sp, "r")) != NULL) { fclose(sf); peered = 1; }
                if (peered) {
                    fprintf(stderr,
                        "ais: note -- this index syncs, and --set does not propagate. "
                        "The peer still holds the old value and will feed it back; "
                        "run --set on each device, or --del the record and re-add it.\n");
                }
            }
            break;
        }
        case CMD_UPDATE: {
            long id;
            if (optind >= argc) die("--update needs an ID");
            id = atol(argv[optind]);
            /* keys after the ID; -key detaches. Leading-'-' keys need '--':
             *   ais --update 42 rome        (attach rome)
             *   ais --update 42 -- -venice  (detach venice) */
            if (collect_keys(argv, optind + 1, argc, exkeys, nexk, keys, sizeof(keys)) != 0)
                die("key list too long");
            if (keys[0] == '\0')
                die("--update needs at least one key (KEY to add, -KEY to remove)");
            if (ais_update(&a, id, keys) != 0)
                /* ais_update fails for several reasons -- unknown or deleted id, a
                 * key that would push the line past AIS_LINE_MAX, IO. Name the
                 * likely one rather than asserting a cause that may be wrong. */
                die("--update: record %ld unchanged (no such live id, or the keys "
                    "would not fit on one line)", id);
            break;
        }
        case CMD_DEL: {
            long id;
            if (optind >= argc) die("--del needs an ID");
            id = atol(argv[optind]);
            if (!assume_yes) {
                char p[64];
                /* SHOW the record before asking. "Delete record 42?" tells the user
                 * nothing they can check -- an id is not something anyone recognises,
                 * and --del-under now previews what it destroys, so this asking blind
                 * would be the odd one out. */
                long nlink = 0;
                /* ais_record reads the STORE, which still holds tombstoned lines
                 * until compaction. Without this the preview showed a record that
                 * was already deleted and offered to delete it again. */
                int gone = (id > 0) ? tomb_contains(&a, id) : 0;
                if (gone < 0) die("--del: cannot read the tombstone log");
                fprintf(stderr, "About to delete:\n");
                if (!gone)
                    ais_record(&a, id, delkey_line_cb, &nlink);
                if (nlink == 0) {
                    /* Nothing to show means nothing to delete. Printing an empty
                     * manifest and then asking "permanently delete record 42?"
                     * invited a yes to a record that was never there. */
                    fprintf(stderr, "  no live record %ld\n", id);
                    ais_close(&a);
                    return 1;
                }
                if (nlink > 1)
                    fprintf(stderr, "     (+%ld more link%s on this record)\n",
                            nlink - 1, nlink == 2 ? "" : "s");
                fprintf(stderr, "\n");
                snprintf(p, sizeof(p), "Permanently delete record %ld?", id);
                if (!confirm(p)) { fprintf(stderr, "aborted\n"); ais_close(&a); return 1; }
            }
            ais_record(&a, id, shred_value_cb, a.dir);   /* shred encrypted blobs first */
            if (ais_del(&a, id) != 0) die("del failed");
            break;
        }
        case CMD_UNTAG: {
            /* The non-destructive counterpart to --del-under: the records stay,
             * they just stop being filed under KEY. Reversible by re-tagging, so
             * it gets a count and a plain y/N rather than a manifest. */
            const char *key = (optind < argc) ? argv[optind]
                            : (nexk ? exkeys[0] : NULL);
            char p[AIS_KEY_MAX + 128];
            long n = 0;
            char *k1[1];
            if (key == NULL) die("--untag needs a KEY");
            k1[0] = (char *)key;
            if (ais_get(&a, k1, 1, AIS_AND, delkey_count_cb, &n) < 0)
                die("--untag: cannot read '%s'", key);
            if (n == 0) {
                fprintf(stderr, "nothing is filed under '%s'\n", key);
                break;
            }
            snprintf(p, sizeof(p),
                     "Remove the tag '%s' from %ld record%s? The records are kept.",
                     key, n, n == 1 ? "" : "s");
            if (assume_yes || confirm(p)) {
                long done = ais_untag_key(&a, key);
                if (done < 0) die("untag failed");
                printf("untagged %ld\n", done);
            } else { fprintf(stderr, "aborted\n"); ais_close(&a); return 1; }
            break;
        }
        case CMD_DELKEY: {
            const char *key = (optind < argc) ? argv[optind]
                            : (nexk ? exkeys[0] : NULL);
            char p[AIS_KEY_MAX + 128];
            long nprev = 0;              /* how many the manifest counted */
            /* The old spelling reads like "remove the tag", which is the one thing
             * it does not do. Kept working forever; just say so once per use. */
            if (strcmp(cmd_spelling, "del-key") == 0)
                fprintf(stderr, "ais: --del-key is now --del-under "
                                "(it deletes the records, not the tag); "
                                "--untag removes just the tag\n");
            if (key == NULL) die("--%s needs a KEY", cmd_spelling);
            /* SHOW what is about to go before asking. This deletes whole records,
             * not just the key, and a record filed under several keys disappears
             * from all of them -- so a bare "are you sure?" is not enough to
             * consent to. Skipped under -y, which is the caller saying they know. */
            if (!assume_yes) {
                char *k1[1];
                struct delkey_preview pv;
                k1[0] = (char *)key;
                /* Name the DIFFERENCE, not just the danger. "--del-under" still
                 * reads like "remove the tag", and that is the one thing it does
                 * not do: the records themselves go, from every key they are filed
                 * under. A user who meant to untag needs to be told before
                 * answering, and told what to type instead. */
                fprintf(stderr, "--%s deletes RECORDS, not the tag.\n\n"
                                "Filed under '%s' -- all of these will be deleted, and\n"
                                "each disappears from every other key it is filed under too:\n\n",
                        cmd_spelling, key);
                pv.a = &a;
                pv.shown = 0;
                if (ais_get(&a, k1, 1, AIS_AND, delkey_show_cb, &pv) < 0)
                    die("--%s: cannot read '%s'", cmd_spelling, key);
                nprev = pv.shown;
                if (nprev == 0) {
                    fprintf(stderr, "nothing is filed under '%s'\n", key);
                    break;
                }
                if (pv.shown > DELKEY_PREVIEW_MAX)
                    fprintf(stderr, "  ... and %ld more. To see them all: ais %s\n",
                            pv.shown - DELKEY_PREVIEW_MAX, key);
                fprintf(stderr,
                    "\nTo remove the tag and KEEP the records: ais --untag %s\n\n",
                    key);
            }
            /* The count is repeated here because with a long list the header has
             * scrolled off; the prompt is the only line guaranteed to be on screen. */
            snprintf(p, sizeof(p), "Permanently delete %s %ld record%s filed under '%s'?",
                     nprev == 1 ? "this" : "these", nprev, nprev == 1 ? "" : "s", key);
            if (assume_yes || confirm(p)) {
                char *k1[1];
                long n;
                k1[0] = (char *)key;
                ais_get(&a, k1, 1, AIS_AND, shred_id_cb, &a);   /* shred encrypted blobs first */
                n = ais_del_key(&a, key);
                if (n < 0) die("del-key failed");
                printf("deleted %ld\n", n);
            } else { fprintf(stderr, "aborted\n"); ais_close(&a); return 1; }
            break;
        }
        case CMD_COMPACT:
            if (purge_deletes) {
                /* Name the price before asking. Forgetting a deletion is the one
                 * thing here that another device can undo for you, and the user
                 * cannot be expected to know that a peer still holds the record. */
                if (!assume_yes) {
                    fprintf(stderr,
                        "This also FORGETS what was deleted.\n\n"
                        "Each deletion keeps working on this device, but stops being\n"
                        "something another device can be told about -- and stops being\n"
                        "something anyone holding your files could test a guess against.\n\n"
                        "Sync your other devices FIRST. A device that has not seen these\n"
                        "deletions can send those records back.\n\n");
                }
                if (assume_yes ||
                    confirm("Reclaim space and permanently forget what was deleted?")) {
                    if (ais_compact_purge(&a) != 0) die("compact failed");
                } else { fprintf(stderr, "aborted\n"); ais_close(&a); return 1; }
            } else if (assume_yes ||
                confirm("Compaction reclaims the space of deleted records. "
                        "The deletions themselves are kept. Proceed?")) {
                if (ais_compact(&a) != 0) die("compact failed");
            } else { fprintf(stderr, "aborted\n"); ais_close(&a); return 1; }
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
        case CMD_DEFAULT:
            if (optind < argc) {              /* set (empty arg clears it) */
                if (ais_default_set(argv[optind]) != 0)
                    die("default: cannot write ~/.ais/config");
                if (argv[optind][0] == '\0') printf("default index cleared\n");
                else printf("default index: %s\n", argv[optind]);
            } else {                          /* show */
                char cur[AIS_PATH_MAX];
                printf("%s\n", ais_default_get(cur, sizeof cur) == 1
                                   ? cur : "(no saved default; using ~/.ais)");
            }
            break;
        case CMD_SWITCH: {
            char path[AIS_PATH_MAX];
            if (optind >= argc) {                 /* no NAME -> show current */
                char cur[AIS_KEY_MAX];
                if (ais_current_get(cur, sizeof cur) == 1
                    && cur[0] != '\0' && strcmp(cur, "home") != 0) {
                    if (ais_index_path(cur, path, sizeof path) != 1)
                        snprintf(path, sizeof path, "(unregistered)");
                    printf("%s\t%s\n", cur, path);
                } else {
                    if (ais_home_path(path, sizeof path) != 0) die("no home dir");
                    printf("home\t%s\n", path);
                }
                break;
            }
            {
                const char *name = argv[optind];
                if (create) {                     /* -c: create a new index + switch */
                    ais nb;
                    char dir[AIS_PATH_MAX];
                    if (optind + 1 < argc) {
                        if (snprintf(dir, sizeof dir, "%s", argv[optind + 1]) >= (int)sizeof dir)
                            die("switch -c: DIR too long");
                    } else if (ais_index_default_dir(name, dir, sizeof dir) != 0) {
                        die("switch -c: cannot form a default dir for '%s'", name);
                    }
                    if (ais_index_add(name, dir) != 0)
                        die("switch -c: cannot register '%s' ('home' is reserved)", name);
                    if (ais_open(&nb, dir) != 0)
                        die("switch -c: cannot create index at %s", dir);
                    ais_close(&nb);
                    fprintf(stderr, "created index '%s' at %s\n", name, dir);
                } else if (strcmp(name, "home") != 0
                           && ais_index_path(name, path, sizeof path) != 1) {
                    die("switch: no index named '%s' "
                        "(create: ais --switch -c %s [DIR]; list: ais --indexes)", name, name);
                }
                if (ais_current_set(name) != 0)
                    die("switch: cannot set current to '%s'", name);
                printf("switched to %s\n", name);
            }
            break;
        }
        case CMD_INDEXES: {
            char cur[AIS_KEY_MAX], home[AIS_PATH_MAX];
            struct index_ctx ic;
            int cur_named = (ais_current_get(cur, sizeof cur) == 1
                             && cur[0] != '\0' && strcmp(cur, "home") != 0);
            if (ais_home_path(home, sizeof home) != 0) die("no home dir");
            printf("%c home\t%s\n", cur_named ? ' ' : '*', home);
            ic.cur = cur_named ? cur : "";
            if (ais_index_list(print_index, &ic) < 0) die("indexes failed");
            break;
        }
        case CMD_FORGET: {
            char cur[AIS_KEY_MAX];
            if (optind >= argc) die("--forget needs a NAME");
            if (ais_current_get(cur, sizeof cur) == 1 && strcmp(cur, argv[optind]) == 0)
                die("--forget: '%s' is the current index; switch away first", argv[optind]);
            if (ais_index_remove(argv[optind]) != 0)
                die("--forget: cannot remove '%s' ('home' is reserved)", argv[optind]);
            printf("forgot '%s' (its data dir was left untouched)\n", argv[optind]);
            break;
        }
        case CMD_DOC:
            if (collect_keys(argv, optind, argc, exkeys, nexk, keys, sizeof(keys)) != 0)
                die("key list too long");
            build_keys(project, keys, full, sizeof(full));
            if (encrypt)
                feed_encrypt_doc(&a, full);       /* mode 2: big encrypted note -> blob */
            else
                feed_doc(&a, full);
            break;
        }
        ais_close(&a);
        return 0;
    }

    /* ---- save (put mode): -v or -i ---- */
    if (nval > 0 || interactive || encrypt) {
        /* A key beginning '-' cannot be stored: '-key' is the DETACH operator
         * in this grammar (ais --update ID -KEY), and put shares the posting
         * pass with update, so such a token is dropped before it can emit a
         * detach on a brand-new record. That is correct, but it used to be
         * SILENT: `ais -v note -- -todo` stored an untagged, unfindable
         * record and exited 0. Say so instead. */
        {
            int ki;
            for (ki = optind; ki < argc; ki++)
                if (argv[ki][0] == '-' && argv[ki][1] != '\0')
                die("a key cannot begin with '-' (that is the detach operator): %s",
                    argv[ki]);
            for (ki = 0; ki < nexk; ki++)
                if (exkeys[ki][0] == '-' && exkeys[ki][1] != '\0')
                die("a key cannot begin with '-' (that is the detach operator): %s",
                    exkeys[ki]);
        }
        if (collect_keys(argv, optind, argc, exkeys, nexk, keys, sizeof(keys)) != 0)
            die("key list too long");
        build_keys(project, keys, full, sizeof(full));

        if (encrypt) {                            /* -e: store one value encrypted */
            if (full[0] == '\0')
                die("-e needs at least one KEY");
            if (nval >= 1 && strcmp(values[0], "-") != 0)
                die("-e: don't pass the secret via -v (it lands in ps and shell "
                    "history); omit -v to be prompted, or use -v - to read stdin");
            feed_encrypt(&a, full, nval == 1);    /* nval==1 means -v - (stdin) */
            ais_close(&a);
            return 0;
        }

        if (interactive) {
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
