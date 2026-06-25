/* feed.c -- bulk feeding values into the index: stdin lines (`-v -`),
 * interactive (`-i`), --import, and the CLI --doc streaming. One aspect, "file
 * many values under given keys", kept out of main.c so the CLI dispatcher stays
 * linear.
 *
 * Front-end code: it may die() on error, the same as main.c (the engine
 * modules only return codes). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doc.h"
#include "feed.h"
#include "log.h"
#include "secret.h"

/* feed_stdin: file each non-empty stdin line, verbatim, as a value under KEYS. */
void feed_stdin(ais *a, const char *keys)
{
    char line[AIS_LINE_MAX];

    while (fgets(line, sizeof(line), stdin) != NULL) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        if (n == 0)
            continue;                  /* skip blank lines */
        if (ais_put(a, keys, line) < 0)
            die("put -: failed on '%s'", line);
    }
}

/* Strip a trailing newline/CR in place. */
static void chomp(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

void feed_interactive(ais *a, const char *base)
{
    const char *ttypath = getenv("AIS_TTY");   /* a file overrides the terminal */
    FILE *tty;
    char value[AIS_LINE_MAX];
    char typed[AIS_LINE_MAX];
    char keys[AIS_LINE_MAX];

#ifdef _WIN32
    tty = fopen(ttypath != NULL ? ttypath : "CONIN$", "r");   /* Windows console */
#else
    tty = fopen(ttypath != NULL ? ttypath : "/dev/tty", "r");
#endif
    if (tty == NULL)
        die("put -i: no terminal for keys (pipe values in, or set AIS_TTY=FILE)");

    /* Each stdin line is a value; ask the terminal for its keys (Enter accepts
     * the base keys). Values flow from the pipe, keys from the tty -- two
     * separate streams, which is the whole point of -i. */
    while (fgets(value, sizeof(value), stdin) != NULL) {
        chomp(value);
        if (value[0] == '\0')
            continue;                          /* skip blank input lines */

        if (base[0] != '\0')
            fprintf(stderr, "%s\n  keys (added to: %s) > ", value, base);
        else
            fprintf(stderr, "%s\n  keys > ", value);
        fflush(stderr);

        if (fgets(typed, sizeof(typed), tty) == NULL)
            break;                             /* EOF on the terminal -> done */
        chomp(typed);

        /* keys = base, then " " + typed if any. Two writes (not one "%s %s")
         * so the compiler can't flag a possible truncation. */
        {
            size_t kl = 0;
            keys[0] = '\0';
            if (base[0] != '\0') {
                int n = snprintf(keys, sizeof(keys), "%s", base);
                kl = (n > 0) ? (size_t)n : 0;
            }
            if (typed[0] != '\0' && kl < sizeof(keys))
                snprintf(keys + kl, sizeof(keys) - kl, "%s%s",
                         kl > 0 ? " " : "", typed);
        }

        if (keys[0] == '\0') {
            fprintf(stderr, "  (no keys given; skipped)\n");
            continue;
        }
        if (ais_put(a, keys, value) < 0)
            die("put -i: failed on '%s'", value);
    }
    fclose(tty);
}

void feed_import(ais *a)
{
    char line[AIS_LINE_MAX];
    long n = 0;

    /* Each line is "keys|value" (the inverse of dump's id|keys|value). Keys
     * never contain '|' (key_encode maps it to '_'), so the FIRST '|' is the
     * keys/value split; the value may itself contain '|' and is taken verbatim.
     * Blank lines and #-comments are skipped, so the file stays hand-editable. */
    while (fgets(line, sizeof(line), stdin) != NULL) {
        char *bar, *keys, *val, *e;

        chomp(line);
        if (line[0] == '\0' || line[0] == '#')
            continue;
        bar = strchr(line, '|');
        if (bar == NULL) {
            fprintf(stderr, "import: no '|', skipped: %s\n", line);
            continue;
        }
        *bar = '\0';

        keys = line;
        while (*keys == ' ' || *keys == '\t')
            keys++;                            /* trim around the separator so */
        for (e = bar - 1; e >= keys && (*e == ' ' || *e == '\t'); e--)
            *e = '\0';                         /* "keys | value" works too     */
        val = bar + 1;
        while (*val == ' ' || *val == '\t')
            val++;

        if (*keys == '\0') {
            fprintf(stderr, "import: empty keys, skipped: %s\n", val);
            continue;
        }
        if (ais_put(a, keys, val) < 0)
            die("import: failed on '%s'", val);
        n++;
    }
    fprintf(stderr, "imported %ld\n", n);
}

/* CLI --doc: stream stdin (any size, bounded memory) into a blob, then put its
 * path. The blob naming and the path-put are shared with the GUIs via doc.c;
 * only the streaming-from-stdin part is CLI-specific, so it stays here. */
void feed_doc(ais *a, const char *keys)
{
    char blobpath[AIS_PATH_MAX], relval[AIS_PATH_MAX];
    FILE *bf;
    char buf[8192];
    size_t n;
    long got;

    if (ais_doc_blobname(a, relval, sizeof(relval), blobpath, sizeof(blobpath)) != 0)
        die("doc: cannot prepare blob path under '%s'", a->dir);

    bf = fopen(blobpath, "w");
    if (bf == NULL)
        die("doc: cannot write '%s'", blobpath);
    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0)
        if (fwrite(buf, 1, n, bf) != n) {
            fclose(bf);
            die("doc: write failed");
        }
    if (fclose(bf) != 0)
        die("doc: close failed");

    /* store the relative path as the value */
    got = ais_put(a, keys, relval);
    if (got < 0)
        die("doc: put failed");
    printf("%ld|%s\n", got, relval);
}

/* feed_import_interactive: --import with a per-record gate. Each stdin line is a
 * "keys|value" record (a --dump of someone else's index, or a shared keys|value
 * file); we show it and read a [y/N] answer from the terminal -- only y/Y takes
 * it, N (the default, and a bare Enter) skips. Records come on stdin, answers on
 * the tty (/dev/tty, or $AIS_TTY for scripting/tests), so the two streams stay
 * apart, exactly as -i keeps values and keys apart. For sipping the useful bits
 * of a shared index without pulling in (and polluting yours with) everything. */
void feed_import_interactive(ais *a)
{
    const char *ttypath = getenv("AIS_TTY");   /* a file overrides the terminal */
    FILE *tty;
    char line[AIS_LINE_MAX];
    char ans[16];
    long added = 0, seen = 0;

#ifdef _WIN32
    tty = fopen(ttypath != NULL ? ttypath : "CONIN$", "r");
#else
    tty = fopen(ttypath != NULL ? ttypath : "/dev/tty", "r");
#endif
    if (tty == NULL)
        die("import-interactively: no terminal for y/N (set AIS_TTY=FILE)");

    /* Same "keys|value" parse as feed_import; the only addition is the prompt. */
    while (fgets(line, sizeof(line), stdin) != NULL) {
        char *bar, *keys, *val, *e;

        chomp(line);
        if (line[0] == '\0' || line[0] == '#')
            continue;
        bar = strchr(line, '|');
        if (bar == NULL) {
            fprintf(stderr, "import: no '|', skipped: %s\n", line);
            continue;
        }
        *bar = '\0';
        keys = line;
        while (*keys == ' ' || *keys == '\t')
            keys++;
        for (e = bar - 1; e >= keys && (*e == ' ' || *e == '\t'); e--)
            *e = '\0';
        val = bar + 1;
        while (*val == ' ' || *val == '\t')
            val++;
        if (*keys == '\0') {
            fprintf(stderr, "import: empty keys, skipped: %s\n", val);
            continue;
        }
        seen++;

        fprintf(stderr, "%s | %s\n  take into your index? [y/N] ", keys, val);
        fflush(stderr);
        if (fgets(ans, sizeof(ans), tty) == NULL)
            break;                              /* EOF on the terminal -> done */
        if (ans[0] == 'y' || ans[0] == 'Y') {
            if (ais_put(a, keys, val) < 0)
                die("import-interactively: failed on '%s'", val);
            added++;
        }
    }
    fclose(tty);
    fprintf(stderr, "imported %ld of %ld\n", added, seen);
}

void feed_encrypt(ais *a, const char *keys, int from_stdin)
{
    char val[1024], pw[1024], marked[4096];
    long vn, mn, id;

    if (from_stdin) {                      /* -v - : the value comes from stdin */
        size_t got = fread(val, 1, sizeof val - 1, stdin);
        if (got > 0 && val[got - 1] == '\n')
            got--;                         /* a one-line piped secret */
        val[got] = '\0';
        vn = (long)got;
    } else {                               /* prompt for it, echo off, off the tty */
        vn = secret_prompt("secret value: ", 0, val, sizeof val);
    }
    if (vn < 0)
        die("-e: no value (or crypto not built; run crypto/vendor-monocypher.sh)");

    if (secret_prompt("passphrase: ", 1, pw, sizeof pw) < 0) {
        secret_wipe(val, sizeof val);
        die("-e: passphrase entry failed");
    }

    mn = secret_encrypt((const unsigned char *)val, (size_t)vn,
                        (const unsigned char *)pw, strlen(pw), marked, sizeof marked);
    secret_wipe(val, sizeof val);
    secret_wipe(pw, sizeof pw);
    if (mn < 0)
        die("-e: encryption failed (value too large to inline? big notes: use --doc)");

    id = ais_put(a, keys, marked);
    secret_wipe(marked, sizeof marked);
    if (id < 0)
        die("-e: store failed");
    printf("%ld\n", id);
}
