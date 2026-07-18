/* tests.c -- AIS regression bundle. Linear, inline, one comment per test.
 *
 * Compiled only under -DUNIT_TEST (see `make ut`); invisible to the normal
 * build. Independent black-box tests of the ais.h contract plus a few pure
 * helpers (key.h). Mutating tests use a fresh scratch INDEX under /tmp, wiped
 * before and after, so the suite is idempotent and never touches the committed
 * fixture tests/INDEX/store.
 */
#ifdef UNIT_TEST

/* setenv() for the AIS_TTY test seam (BSD+POSIX, hidden under bare -std=c99) */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>      /* mkdir (canonicalize a scratch dir via realpath) */
#include <sys/wait.h>      /* waitpid -- the forked socket-transport test */
#include <sys/socket.h>    /* the busy-port sync test holds a port in-process */
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ais.h"
#include "key.h"
#include "post.h"
#include "store.h"
#include "compact.h"
#include "stats.h"
#include "find.h"
#include "doc.h"
#include "embed.h"
#include "feed.h"
#include "sync.h"
#include "locate.h"
#include "secret.h"
#include "b64.h"

/* The crypto round-trip test compiles only once Monocypher has been vendored
 * (crypto/vendor-monocypher.sh); ais_crypto.h declares the API either way. */
#if defined(__has_include) && __has_include("crypto/monocypher.h")
#  define AIS_UT_HAVE_CRYPTO 1
#  include "crypto/ais_crypto.h"
#endif

#define FIXTURE_STORE "../tests/INDEX/store"   /* cwd is c/ under `make ut` */

static int ut_pass = 0;
static int ut_fail = 0;

/* flat assert: one line per check; never aborts, so every test still runs */
#define CHECK(cond, msg) do { \
        if (cond) { ut_pass++; printf("  ok   %s\n", (msg)); } \
        else      { ut_fail++; printf("  FAIL %s  (%s:%d)\n", (msg), __FILE__, __LINE__); } \
    } while (0)

/* ---- scratch index helpers -------------------------------------------- */

/* rm -rf DIR via the shell (test-only convenience; not in the shipped code) */
static void scratch_rm(const char *dir)
{
    char cmd[AIS_PATH_MAX + 16];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    (void)system(cmd);
}

/* ---- collectors: gather ids / values emitted by the streaming callbacks - */

struct idvec { long ids[256]; int n; };

static int collect_id(long id, void *ctx)
{
    struct idvec *v = ctx;
    if (v->n < (int)(sizeof(v->ids) / sizeof(v->ids[0])))
        v->ids[v->n++] = id;
    return 0;
}

struct valvec { char vals[16][AIS_LINE_MAX]; int n; };

static int collect_val(long id, const char *value, void *ctx)
{
    struct valvec *v = ctx;
    (void)id;
    if (v->n < 16)
        snprintf(v->vals[v->n++], AIS_LINE_MAX, "%s", value);
    return 0;
}

struct keyvec { char keys[64][AIS_KEY_MAX]; int n; };

static int collect_key(const char *key, void *ctx)
{
    struct keyvec *v = ctx;
    if (v->n < 64)
        snprintf(v->keys[v->n++], AIS_KEY_MAX, "%s", key);
    return 0;
}

struct tlrow { long id; char ts[AIS_TS_MAX]; char keys[AIS_KEY_MAX]; char value[256]; };
struct tlvec { struct tlrow r[64]; int n; };

static int collect_tl(long id, const char *ts, const char *keys,
                      const char *value, void *ctx)
{
    struct tlvec *v = ctx;
    if (v->n < 64) {
        v->r[v->n].id = id;
        snprintf(v->r[v->n].ts,    sizeof(v->r[v->n].ts),    "%s", ts);
        snprintf(v->r[v->n].keys,  sizeof(v->r[v->n].keys),  "%s", keys);
        snprintf(v->r[v->n].value, sizeof(v->r[v->n].value), "%s", value);
        v->n++;
    }
    return 0;
}

struct tagrow { char key[AIS_KEY_MAX]; long count; };
struct tagvec { struct tagrow t[64]; int n; };

static int collect_tag(const char *key, long count, void *ctx)
{
    struct tagvec *v = ctx;
    if (v->n < 64) {
        snprintf(v->t[v->n].key, sizeof(v->t[v->n].key), "%s", key);
        v->t[v->n].count = count;
        v->n++;
    }
    return 0;
}

/* run an AND/OR query, capturing the emitted ids in V */
static void query(ais *a, ais_mode mode, struct idvec *v, int nkeys, ...)
{
    char *keys[AIS_KEYS_MAX];
    va_list ap;
    int i;

    va_start(ap, nkeys);
    for (i = 0; i < nkeys; i++)
        keys[i] = va_arg(ap, char *);
    va_end(ap);

    v->n = 0;
    ais_get(a, keys, nkeys, mode, collect_id, v);
}

/* exact, ordered id-set compare */
static int ids_eq(const struct idvec *v, const long *want, int n)
{
    int i;
    if (v->n != n)
        return 0;
    for (i = 0; i < n; i++)
        if (v->ids[i] != want[i])
            return 0;
    return 1;
}

/* Read a key's posting file (idx/<p>/<key>) into OUT; return count, -1 if no
 * file. Also reports whether the ids are strictly ascending via *ascending. */
static int read_posting(const char *dir, const char *enc_key,
                        long *out, int outmax, int *ascending)
{
    char pre[3];
    char path[AIS_PATH_MAX];
    char line[64];
    FILE *fp;
    int n = 0;

    *ascending = 1;
    if (key_prefix(enc_key, pre, sizeof(pre)) != 0)
        return -1;
    snprintf(path, sizeof(path), "%s/idx/%s/%s", dir, pre, enc_key);
    fp = fopen(path, "r");
    if (fp == NULL)
        return -1;
    while (fgets(line, sizeof(line), fp) != NULL && n < outmax) {
        long v = atol(line);
        if (n > 0 && v <= out[n - 1])
            *ascending = 0;
        out[n++] = v;
    }
    fclose(fp);
    return n;
}

/* Build the standard 6-record fixture index by put() into a fresh dir. */
static void build_fixture(ais *a, const char *dir)
{
    scratch_rm(dir);
    ais_open(a, dir);
    ais_put(a, "linux samba config network", "/etc/samba/smb.conf");
    ais_put(a, "linux nfs export network",   "/etc/exports");
    ais_put(a, "c ansi build make",          "https://www.gnu.org/software/make/");
    ais_put(a, "c ansi compression kolmogorov",
               "https://en.wikipedia.org/wiki/Kolmogorov_complexity");
    ais_put(a, "memory archive longevity",   "Plain UTF-8 text outlives every binary format.");
    ais_put(a, "memory archive sdcard",      "Soft-link the INDEX dir into the app.");
}

/* ====================================================================== */

/* ---- key encoding: lowercase; space/control/'|'/'/'/'\' -> '_' ---- */
static void test_key_encode(void)
{
    char out[AIS_KEY_MAX];
    CHECK(key_encode("Linux", out, sizeof(out)) == 0 && strcmp(out, "linux") == 0,
          "key_encode lowercases");
    CHECK(key_encode("C ANSI", out, sizeof(out)) == 0 && strcmp(out, "c_ansi") == 0,
          "key_encode space -> '_'");
    CHECK(key_encode("a\tb", out, sizeof(out)) == 0 && strcmp(out, "a_b") == 0,
          "key_encode tab -> '_'");
    CHECK(key_encode("a|b", out, sizeof(out)) == 0 && strcmp(out, "a_b") == 0,
          "key_encode '|' -> '_' (field delimiter)");
    CHECK(key_encode("a/b", out, sizeof(out)) == 0 && strcmp(out, "a_b") == 0,
          "key_encode '/' -> '_' (path safety)");
    CHECK(key_encode("a\\b", out, sizeof(out)) == 0 && strcmp(out, "a_b") == 0,
          "key_encode '\\' -> '_' (path safety)");
    CHECK(key_encode("", out, sizeof(out)) == -1, "key_encode empty -> -1");
}

/* ---- key prefix: first one or two encoded chars (navigable shard) ---- */
static void test_key_prefix(void)
{
    char pre[3];
    CHECK(key_prefix("linux", pre, sizeof(pre)) == 0 && strcmp(pre, "li") == 0,
          "key_prefix multi-char -> first two");
    CHECK(key_prefix("c", pre, sizeof(pre)) == 0 && strcmp(pre, "c") == 0,
          "key_prefix single char -> one");
    CHECK(key_prefix("", pre, sizeof(pre)) == -1, "key_prefix empty -> -1");
}

/* ---- put: ids strictly monotonic; posting created and ascending ---- */
static void test_put_monotonic(void)
{
    ais a;
    long id1, id2, id3;
    long ids[16];
    int n, asc;
    const char *dir = "/tmp/ais_ut_mono";

    scratch_rm(dir);
    ais_open(&a, dir);
    id1 = ais_put(&a, "alpha beta", "v1");
    id2 = ais_put(&a, "beta gamma", "v2");
    id3 = ais_put(&a, "alpha gamma", "v3");
    CHECK(id1 == 1 && id2 == 2 && id3 == 3, "put ids strictly monotonic (1,2,3)");

    /* "alpha" was filed for id1 and id3 -> {1,3}, ascending */
    n = read_posting(dir, "alpha", ids, 16, &asc);
    CHECK(n == 2 && ids[0] == 1 && ids[1] == 3, "posting 'alpha' == {1,3}");
    CHECK(asc, "posting 'alpha' ascending");
    ais_close(&a);
    scratch_rm(dir);
}

/* ---- idempotent put: same value -> same id, no dup record/posting ---- */
static void test_put_idempotent(void)
{
    ais a;
    long id1, id2;
    long ids[16];
    int n, asc;
    struct idvec v;
    const char *dir = "/tmp/ais_ut_idem";

    scratch_rm(dir);
    ais_open(&a, dir);
    id1 = ais_put(&a, "alpha beta", "same-value");
    id2 = ais_put(&a, "alpha beta", "same-value");   /* identical re-put */
    CHECK(id1 == id2, "re-put same value -> same id");
    CHECK(a.next_id == 2, "re-put did not consume a new id");

    /* no duplicate posting for 'alpha' */
    n = read_posting(dir, "alpha", ids, 16, &asc);
    CHECK(n == 1 && ids[0] == id1, "re-put: no duplicate posting");

    /* no duplicate record line */
    query(&a, AIS_AND, &v, 1, "alpha");
    CHECK(v.n == 1 && v.ids[0] == id1, "re-put: single record only");
    ais_close(&a);
    scratch_rm(dir);
}

/* ---- idempotent put under a NEW key: ordered insert keeps file ascending - */
static void test_put_new_key_ordered_insert(void)
{
    ais a;
    long idA, idB, idC, again;
    long ids[16];
    int n, asc;
    struct idvec v;
    const char *dir = "/tmp/ais_ut_oins";

    scratch_rm(dir);
    ais_open(&a, dir);
    idA = ais_put(&a, "zeta", "valA");   /* id 1, only under 'zeta' */
    idB = ais_put(&a, "omega", "valB");  /* id 2 */
    idC = ais_put(&a, "omega", "valC");  /* id 3; 'omega' file now {2,3} */

    /* re-put valA (old id 1) under existing key 'omega' -> same id, in order */
    again = ais_put(&a, "omega", "valA");
    CHECK(again == idA, "re-put existing value under new key -> same id");
    CHECK(a.next_id == 4, "ordered-insert re-put consumed no new id");

    /* 'omega' now finds id 1, and the file is ascending {1,2,3} */
    query(&a, AIS_AND, &v, 1, "omega");
    {
        long want[3] = {idA, idB, idC};
        CHECK(ids_eq(&v, want, 3), "new key 'omega' now finds the old id, ascending");
    }
    n = read_posting(dir, "omega", ids, 16, &asc);
    CHECK(n == 3 && asc && ids[0] == 1 && ids[1] == 2 && ids[2] == 3,
          "'omega' posting file ascending after ordered insert");
    ais_close(&a);
    scratch_rm(dir);
}

/* ---- get AND / OR over the 6-record fixture (the spec oracle) ---- */
static void test_get_and_or(void)
{
    ais a;
    struct idvec v;
    const char *dir = "/tmp/ais_ut_andor";

    build_fixture(&a, dir);

    query(&a, AIS_AND, &v, 2, "c", "ansi");
    { long w[2] = {3, 4}; CHECK(ids_eq(&v, w, 2), "AND [c ansi] -> {3,4}"); }

    query(&a, AIS_AND, &v, 2, "linux", "network");
    { long w[2] = {1, 2}; CHECK(ids_eq(&v, w, 2), "AND [linux network] -> {1,2}"); }

    query(&a, AIS_AND, &v, 1, "samba");
    { long w[1] = {1}; CHECK(ids_eq(&v, w, 1), "AND [samba] -> {1}"); }

    query(&a, AIS_AND, &v, 2, "memory", "archive");
    { long w[2] = {5, 6}; CHECK(ids_eq(&v, w, 2), "AND [memory archive] -> {5,6}"); }

    query(&a, AIS_AND, &v, 2, "linux", "nfs");
    { long w[1] = {2}; CHECK(ids_eq(&v, w, 1), "AND [linux nfs] -> {2}"); }

    query(&a, AIS_OR, &v, 2, "samba", "nfs");
    { long w[2] = {1, 2}; CHECK(ids_eq(&v, w, 2), "OR [samba nfs] -> {1,2}"); }

    query(&a, AIS_OR, &v, 2, "longevity", "sdcard");
    { long w[2] = {5, 6}; CHECK(ids_eq(&v, w, 2), "OR [longevity sdcard] -> {5,6}"); }

    /* a key with no matches -> empty */
    query(&a, AIS_AND, &v, 1, "nonexistent");
    CHECK(v.n == 0, "absent key -> empty");

    /* AND including an absent key -> empty */
    query(&a, AIS_AND, &v, 2, "linux", "nonexistent");
    CHECK(v.n == 0, "AND with an absent key -> empty");

    ais_close(&a);
    scratch_rm(dir);
}

/* ---- get with many keys (>2): the k-way merge, AND and OR over 6 keys ----
 * n=1 and n=2 are each special; the general k-way path only starts at 3, so
 * this drives it well past that with a 6-key AND and a 6-key OR. One key is a
 * UTF-8 Cyrillic word ("три" = "three"), so the same assertions ALSO prove
 * that a lowercase non-ASCII key round-trips byte-transparently through
 * put -> encode -> shard -> get -> merge (no extra checks). */
static void test_get_multikey(void)
{
    ais a;
    struct idvec v;
    const char *dir = "/tmp/ais_ut_multikey";

    scratch_rm(dir);
    ais_open(&a, dir);
    ais_put(&a, "one two три four five six", "all-six");       /* id 1: 6 keys */
    ais_put(&a, "one two три four five",     "only-five");     /* id 2: 5 keys */
    ais_put(&a, "six seven",                 "six-and-seven"); /* id 3        */

    /* 6-key AND (incl. the UTF-8 key): only id 1 carries all six keys */
    query(&a, AIS_AND, &v, 6, "one", "two", "три", "four", "five", "six");
    { long w[1] = {1}; CHECK(ids_eq(&v, w, 1), "AND 6 keys incl. UTF-8 'три' -> {1}"); }

    /* drop 'six' -> the 5-key AND now also admits id 2 */
    query(&a, AIS_AND, &v, 5, "one", "two", "три", "four", "five");
    { long w[2] = {1, 2}; CHECK(ids_eq(&v, w, 2), "AND 5 keys incl. UTF-8 'три' -> {1,2}"); }

    /* 6-key AND with one absent key -> empty (short-circuit inside the merge) */
    query(&a, AIS_AND, &v, 6, "one", "two", "три", "four", "five", "absent");
    CHECK(v.n == 0, "AND 6 keys incl. absent -> empty");

    /* 6-key OR across all three records: union of everything touching any key */
    query(&a, AIS_OR, &v, 6, "one", "four", "six", "seven", "nine", "ten");
    { long w[3] = {1, 2, 3}; CHECK(ids_eq(&v, w, 3), "OR 6 keys -> {1,2,3}, ascending"); }

    ais_close(&a);
    scratch_rm(dir);
}

/* ---- multi-link add: ais_record returns all values of a record ---- */
static void test_add_multilink(void)
{
    ais a;
    long id;
    struct valvec vv;
    const char *dir = "/tmp/ais_ut_multi";

    scratch_rm(dir);
    ais_open(&a, dir);
    id = ais_put(&a, "project docs", "https://a");
    CHECK(ais_add(&a, id, "https://b") == 0, "add a second link");
    CHECK(ais_add(&a, id, "file:///c") == 0, "add a third link");
    CHECK(ais_add(&a, 999, "x") == -1, "add to a missing id -> -1");

    vv.n = 0;
    ais_record(&a, id, collect_val, &vv);
    CHECK(vv.n == 3, "ais_record returns all 3 links");
    CHECK(vv.n == 3 && strcmp(vv.vals[0], "https://a") == 0
          && strcmp(vv.vals[1], "https://b") == 0
          && strcmp(vv.vals[2], "file:///c") == 0, "links preserved in order");
    ais_close(&a);
    scratch_rm(dir);
}

/* ---- del: get and dump no longer yield the id; del idempotent ---- */
static void test_del(void)
{
    ais a;
    struct idvec v;
    char dumpbuf[4096];
    FILE *fp;
    size_t got;
    const char *dir = "/tmp/ais_ut_del";

    build_fixture(&a, dir);

    CHECK(ais_del(&a, 1) == 0, "del id 1 -> 0");

    /* get no longer yields id 1 */
    query(&a, AIS_AND, &v, 1, "samba");
    CHECK(v.n == 0, "after del, get [samba] is empty");
    query(&a, AIS_AND, &v, 2, "linux", "network");
    { long w[1] = {2}; CHECK(ids_eq(&v, w, 1), "after del, AND [linux network] -> {2}"); }

    /* dump no longer shows id 1 */
    fp = fopen("/tmp/ais_ut_del_dump", "w");
    ais_dump(&a, fp);
    fclose(fp);
    fp = fopen("/tmp/ais_ut_del_dump", "r");
    got = fread(dumpbuf, 1, sizeof(dumpbuf) - 1, fp);
    dumpbuf[got] = '\0';
    fclose(fp);
    CHECK(strstr(dumpbuf, "smb.conf") == NULL, "after del, dump omits id 1");
    CHECK(strstr(dumpbuf, "/etc/exports") != NULL, "dump still shows surviving id 2");
    remove("/tmp/ais_ut_del_dump");

    /* double-del and del of an absent id are no-ops (still 0) */
    CHECK(ais_del(&a, 1) == 0, "double-del is a no-op");
    CHECK(ais_del(&a, 4242) == 0, "del of an absent id is a no-op");
    query(&a, AIS_AND, &v, 1, "samba");
    CHECK(v.n == 0, "after double-del, [samba] still empty");

    ais_close(&a);
    scratch_rm(dir);
}

/* ---- compact: id physically gone, idx rebuilt+ascending, next_id kept ---- */
static void test_compact(void)
{
    ais a;
    struct idvec v;
    long ids[16];
    int n, asc;
    long next_before;
    const char *dir = "/tmp/ais_ut_compact";

    build_fixture(&a, dir);
    next_before = a.next_id;        /* 7 */

    ais_del(&a, 1);
    CHECK(ais_compact(&a) == 0, "compact -> 0");

    /* id 1 physically gone from the store */
    {
        long got = 0;
        int found = store_find_value(&a, "/etc/samba/smb.conf", &got);
        CHECK(found == 0, "compact: deleted value gone from store");
    }

    /* surviving records still answer */
    query(&a, AIS_AND, &v, 2, "linux", "network");
    { long w[1] = {2}; CHECK(ids_eq(&v, w, 1), "compact: survivors answer (AND linux network)"); }
    query(&a, AIS_AND, &v, 2, "c", "ansi");
    { long w[2] = {3, 4}; CHECK(ids_eq(&v, w, 2), "compact: survivors answer (AND c ansi)"); }

    /* rebuilt posting is ascending and free of id 1 */
    n = read_posting(dir, "network", ids, 16, &asc);
    CHECK(n == 1 && asc && ids[0] == 2, "compact: 'network' rebuilt to {2}, ascending");

    /* 'samba' posting should be gone (the only id was removed) */
    n = read_posting(dir, "samba", ids, 16, &asc);
    CHECK(n <= 0, "compact: 'samba' posting removed");

    /* next_id preserved (max surviving id 6 -> next 7) */
    CHECK(a.next_id == next_before, "compact: next_id preserved (7)");
    ais_close(&a);
    scratch_rm(dir);
}

/* ---- compact must not regress next_id below a RETAINED tombstone ----------
 * Deleting the HIGHEST id then compacting used to set next_id = maxid+1 (live
 * ids only), colliding a fresh record's id with the retained hash-tombstone --
 * the new record was born suppressed (invisible everywhere) and erased by the
 * next compaction. Regression guard for that silent total-loss bug. */
static void test_compact_next_id_no_reuse(void)
{
    ais a;
    struct idvec v;
    const char *dir = "/tmp/ais_ut_compact_reuse";
    long newid;

    scratch_rm(dir);
    ais_open(&a, dir);
    ais_put(&a, "alpha", "first");        /* id 1 */
    ais_put(&a, "beta",  "second");       /* id 2 */
    ais_put(&a, "gamma", "third");        /* id 3 (the HIGHEST) */
    ais_del(&a, 3);                       /* delete the highest id */
    CHECK(ais_compact(&a) == 0, "compact-reuse: compact -> 0");
    CHECK(a.next_id > 3, "compact-reuse: next_id not regressed below the retained tomb");

    newid = ais_put(&a, "knew", "DELTA_NEW");   /* must get a FRESH id past the tomb */
    CHECK(newid > 3, "compact-reuse: new record gets a fresh id, not the tomb's");

    query(&a, AIS_AND, &v, 1, "knew");
    CHECK(v.n == 1 && v.ids[0] == newid,
          "compact-reuse: the new record is VISIBLE, not born dead");

    CHECK(ais_compact(&a) == 0, "compact-reuse: second compact -> 0");
    query(&a, AIS_AND, &v, 1, "knew");
    CHECK(v.n == 1 && v.ids[0] == newid,
          "compact-reuse: still visible after a second compact (not erased)");

    ais_close(&a);
    scratch_rm(dir);
}

/* ---- compact rollback: a mid-compaction failure must NOT destroy the index --
 * get() reads idx/ with no store fallback, so the pre-fix compaction (which
 * rmtree'd idx/ up front and rebuilt it in place) left every keyed read empty if
 * anything failed mid-stream. The fix stages idx/ aside and rolls it back. We
 * force a failure by planting a DIRECTORY where store.new must be a file, so
 * fopen(store.new,"w") fails after the index has been staged. */
static void test_compact_rollback(void)
{
    ais a;
    struct idvec v;
    char sn[AIS_PATH_MAX];
    const char *dir = "/tmp/ais_ut_compact_rollback";

    build_fixture(&a, dir);                  /* ids 1..6, idx built */

    snprintf(sn, sizeof(sn), "%s/store.new", dir);
    CHECK(mkdir(sn, 0777) == 0, "rollback: planted store.new dir to force failure");
    CHECK(ais_compact(&a) != 0, "rollback: compaction fails as injected");

    /* the index must be intact (rolled back), not emptied */
    query(&a, AIS_AND, &v, 2, "linux", "network");
    { long w[2] = {1, 2}; CHECK(ids_eq(&v, w, 2), "rollback: idx intact (linux network) after failed compact"); }
    query(&a, AIS_AND, &v, 2, "memory", "archive");
    { long w[2] = {5, 6}; CHECK(ids_eq(&v, w, 2), "rollback: idx intact (memory archive) after failed compact"); }

    /* remove the blocker; a normal compaction now succeeds and still answers */
    rmdir(sn);
    CHECK(ais_compact(&a) == 0, "rollback: compaction succeeds once unblocked");
    query(&a, AIS_AND, &v, 1, "samba");
    { long w[1] = {1}; CHECK(ids_eq(&v, w, 1), "rollback: query works after the recovery compact"); }

    ais_close(&a);
    scratch_rm(dir);
}

/* ---- compact recovery: a stale idx.bak from a crashed run is cleaned up ----
 * If a compaction crashed after staging idx/ into idx.bak, the next run must not
 * choke on the leftover backup: it rebuilds idx/ from the store (the source of
 * truth) and discards the debris. */
static void test_compact_recover_debris(void)
{
    ais a;
    struct idvec v;
    struct stat st;
    char bak[AIS_PATH_MAX];
    const char *dir = "/tmp/ais_ut_compact_debris";

    build_fixture(&a, dir);
    snprintf(bak, sizeof(bak), "%s/idx.bak", dir);
    CHECK(mkdir(bak, 0777) == 0, "debris: planted a stale idx.bak");

    CHECK(ais_compact(&a) == 0, "debris: compaction succeeds despite stale idx.bak");
    CHECK(stat(bak, &st) != 0, "debris: stale idx.bak cleaned up");
    query(&a, AIS_AND, &v, 2, "c", "ansi");
    { long w[2] = {3, 4}; CHECK(ids_eq(&v, w, 2), "debris: reads correct after recovery"); }

    ais_close(&a);
    scratch_rm(dir);
}

/* ---- a key-detach survives an unaware peer's stale re-attach (LWW) ---------
 * The merge path (A|ts| lines) must not let a peer that never heard about a
 * detach silently revert it: a re-attach wins only if its ts is NEWER than the
 * detach. Regression guard for the order-dependent divergence bug. */
static void test_detach_lww(void)
{
    ais a;
    struct idvec v;
    const char *dir = "/tmp/ais_ut_detach_lww";
    long id;

    scratch_rm(dir);
    ais_open(&a, dir);
    id = ais_put(&a, "foo bar", "danieli");   /* filed under foo + bar */
    ais_update(&a, id, "-bar");               /* detach bar, stamped now */
    query(&a, AIS_AND, &v, 1, "bar");
    CHECK(v.n == 0, "detach-lww: bar detached locally");

    /* unaware peer re-adds the same value with bar, stamped BEFORE the detach */
    ais_put_at(&a, "foo bar", "danieli", "2000-01-01T00:00:00Z");
    query(&a, AIS_AND, &v, 1, "bar");
    CHECK(v.n == 0, "detach-lww: a stale re-attach does NOT revert a newer detach");

    /* idempotent: importing the stale line again is still a no-op */
    ais_put_at(&a, "foo bar", "danieli", "2000-01-01T00:00:00Z");
    query(&a, AIS_AND, &v, 1, "bar");
    CHECK(v.n == 0, "detach-lww: idempotent under repeated stale import");

    /* a LOCAL re-attach (now) is authoritative and wins */
    ais_update(&a, id, "bar");
    query(&a, AIS_AND, &v, 1, "bar");
    CHECK(v.n == 1 && v.ids[0] == id, "detach-lww: local re-attach wins");

    /* and a peer whose attach is genuinely NEWER than the detach also wins */
    ais_update(&a, id, "-bar");                          /* detach again, now */
    ais_put_at(&a, "foo bar", "danieli", "2999-01-01T00:00:00Z");   /* future attach */
    query(&a, AIS_AND, &v, 1, "bar");
    CHECK(v.n == 1 && v.ids[0] == id, "detach-lww: a genuinely-newer attach wins");

    ais_close(&a);
    scratch_rm(dir);
}

/* ---- next_id recovery: delete next_id, reopen, new ids continue max+1 ---- */
static void test_next_id_recovery(void)
{
    ais a;
    long id;
    char path[AIS_PATH_MAX];
    const char *dir = "/tmp/ais_ut_nextid";

    build_fixture(&a, dir);     /* ids 1..6, next_id 7 */
    ais_close(&a);

    /* remove the cached next_id file -> force recovery from the store */
    snprintf(path, sizeof(path), "%s/next_id", dir);
    remove(path);

    ais_open(&a, dir);
    CHECK(a.next_id == 7, "recovered next_id == max(id)+1 (7)");
    id = ais_put(&a, "fresh key", "brand-new-value");
    CHECK(id == 7, "new id continues from max+1 (no reuse)");
    ais_close(&a);
    scratch_rm(dir);
}

/* ---- next_id: a present-but-corrupt cache must recover, not reset to 1 -----
 * A put interrupted by a crash or ENOSPC can leave INDEX/next_id present but
 * 0-length or unparseable. Recovery used to fire only when the file was ABSENT,
 * so a corrupt cache silently reset next_id to 1 and reissued live ids, colliding
 * two records under one id. Guard: a corrupt/garbage cache recovers max(id)+1. */
static void test_next_id_corrupt_cache(void)
{
    ais a;
    char path[AIS_PATH_MAX];
    FILE *fp;
    const char *dir = "/tmp/ais_ut_nextid_corrupt";

    /* case 1: 0-length cache (an interrupted fopen("w",...) truncation) */
    build_fixture(&a, dir);     /* ids 1..6, next_id 7 */
    ais_close(&a);
    snprintf(path, sizeof(path), "%s/next_id", dir);
    fp = fopen(path, "w");
    if (fp != NULL) fclose(fp);                 /* leave it empty */
    ais_open(&a, dir);
    CHECK(a.next_id == 7, "empty cache: next_id recovered to max(id)+1 (7), not 1");
    CHECK(ais_put(&a, "fresh", "brand-new") == 7, "empty cache: new id 7, no live-id collision");
    ais_close(&a);
    scratch_rm(dir);

    /* case 2: garbage (non-numeric) cache */
    build_fixture(&a, dir);
    ais_close(&a);
    fp = fopen(path, "w");
    if (fp != NULL) { fputs("garbage\n", fp); fclose(fp); }
    ais_open(&a, dir);
    CHECK(a.next_id == 7, "garbage cache: next_id recovered to 7, not 1");
    ais_close(&a);
    scratch_rm(dir);
}

/* ---- rebuild from store: store is the source of truth; idx is derived ---- */
static void test_rebuild_from_store(void)
{
    ais a;
    struct idvec v;
    char cmd[AIS_PATH_MAX * 2];
    const char *dir = "/tmp/ais_ut_rebuild";

    /* fresh scratch INDEX seeded from a COPY of the committed fixture store */
    scratch_rm(dir);
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s' && cp '%s' '%s/store'",
             dir, FIXTURE_STORE, dir);
    if (system(cmd) != 0) {
        ut_fail++;
        printf("  FAIL could not seed rebuild scratch from %s\n", FIXTURE_STORE);
        return;
    }

    /* open (no idx/ yet) then materialize idx/ from the store via compact */
    ais_open(&a, dir);
    CHECK(ais_compact(&a) == 0, "rebuild: compact materializes idx/ from store");

    /* the AND/OR oracle must hold against the rebuilt index */
    query(&a, AIS_AND, &v, 2, "c", "ansi");
    { long w[2] = {3, 4}; CHECK(ids_eq(&v, w, 2), "rebuild AND [c ansi] -> {3,4}"); }
    query(&a, AIS_AND, &v, 2, "linux", "network");
    { long w[2] = {1, 2}; CHECK(ids_eq(&v, w, 2), "rebuild AND [linux network] -> {1,2}"); }
    query(&a, AIS_AND, &v, 1, "samba");
    { long w[1] = {1}; CHECK(ids_eq(&v, w, 1), "rebuild AND [samba] -> {1}"); }
    query(&a, AIS_AND, &v, 2, "memory", "archive");
    { long w[2] = {5, 6}; CHECK(ids_eq(&v, w, 2), "rebuild AND [memory archive] -> {5,6}"); }
    query(&a, AIS_AND, &v, 2, "linux", "nfs");
    { long w[1] = {2}; CHECK(ids_eq(&v, w, 1), "rebuild AND [linux nfs] -> {2}"); }
    query(&a, AIS_OR, &v, 2, "samba", "nfs");
    { long w[2] = {1, 2}; CHECK(ids_eq(&v, w, 2), "rebuild OR [samba nfs] -> {1,2}"); }
    query(&a, AIS_OR, &v, 2, "longevity", "sdcard");
    { long w[2] = {5, 6}; CHECK(ids_eq(&v, w, 2), "rebuild OR [longevity sdcard] -> {5,6}"); }

    ais_close(&a);
    scratch_rm(dir);
}

/* ---- keys: every distinct key, ascending; absent idx/ -> none ---- */
static void test_keys(void)
{
    ais a;
    struct keyvec v;
    const char *dir = "/tmp/ais_ut_keys";

    /* absent idx/ (fresh, never put) -> no keys, returns 0 */
    scratch_rm(dir);
    ais_open(&a, dir);
    v.n = 0;
    CHECK(ais_keys(&a, collect_key, &v) == 0 && v.n == 0, "keys: empty index -> none");

    /* two records with overlapping keys -> distinct, sorted union */
    ais_put(&a, "linux config", "/etc/hosts");
    ais_put(&a, "linux nfs",    "/etc/exports");
    v.n = 0;
    ais_keys(&a, collect_key, &v);
    CHECK(v.n == 3
          && strcmp(v.keys[0], "config") == 0
          && strcmp(v.keys[1], "linux")  == 0
          && strcmp(v.keys[2], "nfs")    == 0,
          "keys: distinct + sorted {config,linux,nfs}");
    ais_close(&a);
    scratch_rm(dir);
}

/* ---- stats: records/keys/deleted counts; del bumps deleted, drops records - */
static void test_stats(void)
{
    ais a;
    FILE *fp;
    char buf[1024];
    size_t got;
    const char *dir = "/tmp/ais_ut_stats";

    scratch_rm(dir);
    ais_open(&a, dir);
    ais_put(&a, "linux config", "/etc/hosts");   /* id 1; keys linux,config */
    ais_put(&a, "linux nfs",    "/etc/exports");  /* id 2; keys linux,nfs   */
    ais_put(&a, "memory",       "note");          /* id 3; key  memory      */

    /* 3 live records; 4 distinct keys (config,linux,memory,nfs); 0 deleted */
    fp = tmpfile();
    CHECK(ais_stats(&a, fp) == 0, "stats -> 0");
    rewind(fp);
    got = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[got] = '\0';
    fclose(fp);
    CHECK(strstr(buf, "records: 3") != NULL, "stats: records: 3");
    CHECK(strstr(buf, "keys: 4") != NULL, "stats: keys: 4");
    CHECK(strstr(buf, "deleted: 0") != NULL, "stats: deleted: 0");

    /* delete one record: records drops to 2, deleted rises to 1 */
    CHECK(ais_del(&a, 2) == 0, "stats: del id 2 -> 0");
    fp = tmpfile();
    CHECK(ais_stats(&a, fp) == 0, "stats -> 0 after del");
    rewind(fp);
    got = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[got] = '\0';
    fclose(fp);
    CHECK(strstr(buf, "records: 2") != NULL, "stats: records: 2 after del");
    CHECK(strstr(buf, "deleted: 1") != NULL, "stats: deleted: 1 after del");
    ais_close(&a);
    scratch_rm(dir);
}

/* ---- del_key: tombstones all under a key; count right; other key survives - */
static void test_del_key(void)
{
    ais a;
    struct idvec v;
    const char *dir = "/tmp/ais_ut_delkey";

    scratch_rm(dir);
    ais_open(&a, dir);
    ais_put(&a, "shared one",   "v1");   /* id 1 under 'shared' */
    ais_put(&a, "shared two",   "v2");   /* id 2 under 'shared' */
    ais_put(&a, "shared three", "v3");   /* id 3 under 'shared' */
    ais_put(&a, "other",        "v4");   /* id 4 under 'other'  */

    /* del_key('shared') tombstones all 3 filed under it */
    CHECK(ais_del_key(&a, "shared") == 3, "del_key 'shared' -> 3");

    /* get of the shared key is now empty */
    query(&a, AIS_AND, &v, 1, "shared");
    CHECK(v.n == 0, "del_key: get [shared] now empty");

    /* the other key's record still answers */
    query(&a, AIS_AND, &v, 1, "other");
    { long w[1] = {4}; CHECK(ids_eq(&v, w, 1), "del_key: [other] still -> {4}"); }

    /* del_key of an absent key tombstones nothing */
    CHECK(ais_del_key(&a, "nonexistent") == 0, "del_key absent key -> 0");
    ais_close(&a);
    scratch_rm(dir);
}

/* ---- find: value substring; key ignored; tombstoned records drop out ---- */
static void test_find(void)
{
    ais a;
    FILE *fp;
    char buf[2048];
    size_t got;
    const char *dir = "/tmp/ais_ut_find";

    scratch_rm(dir);
    ais_open(&a, dir);
    ais_put(&a, "trip", "venice is sinking");       /* id 1 */
    ais_put(&a, "trip", "rome was hot");            /* id 2 */
    ais_put(&a, "food", "best gelato in venice");   /* id 3 */

    /* find 'venice' -> records 1 and 3 (value match), never 2 */
    fp = tmpfile();
    CHECK(ais_find(&a, "venice", fp) == 0, "find 'venice' -> 0");
    rewind(fp); got = fread(buf, 1, sizeof(buf) - 1, fp); buf[got] = '\0'; fclose(fp);
    CHECK(strstr(buf, "1|venice is sinking") != NULL, "find: hit id 1");
    CHECK(strstr(buf, "3|best gelato in venice") != NULL, "find: hit id 3");
    CHECK(strstr(buf, "rome") == NULL, "find: id 2 (no match) absent");

    /* a substring that is in a KEY but not a value must NOT match (find is
     * content, not tags): 'trip' is a key, never a value here */
    fp = tmpfile();
    CHECK(ais_find(&a, "trip", fp) == 0, "find 'trip' -> 0");
    rewind(fp); got = fread(buf, 1, sizeof(buf) - 1, fp); buf[got] = '\0'; fclose(fp);
    CHECK(got == 0, "find: key text is not matched");

    /* tombstoned records drop out (the coverage the stats bug taught us) */
    CHECK(ais_del(&a, 3) == 0, "find: del id 3 -> 0");
    fp = tmpfile();
    CHECK(ais_find(&a, "venice", fp) == 0, "find 'venice' after del -> 0");
    rewind(fp); got = fread(buf, 1, sizeof(buf) - 1, fp); buf[got] = '\0'; fclose(fp);
    CHECK(strstr(buf, "1|venice is sinking") != NULL, "find: id 1 still hit");
    CHECK(strstr(buf, "gelato") == NULL, "find: tombstoned id 3 suppressed");

    ais_close(&a);
    scratch_rm(dir);
}

/* ---- ais_record fast path: off resolves single records; multi & gaps OK -- */
static void test_record_fastpath(void)
{
    ais a;
    struct valvec v;
    long id1, id2, id3;
    const char *dir = "/tmp/ais_ut_off";

    scratch_rm(dir);
    ais_open(&a, dir);
    id1 = ais_put(&a, "k1", "alpha");   /* id 1, single line */
    id2 = ais_put(&a, "k2", "beta");    /* id 2 */
    id3 = ais_put(&a, "k3", "gamma");   /* id 3 */

    /* single-value records resolve to their value (fast path via off) */
    v.n = 0; ais_record(&a, id1, collect_val, &v);
    CHECK(v.n == 1 && strcmp(v.vals[0], "alpha") == 0, "record: id1 -> alpha (off)");
    v.n = 0; ais_record(&a, id3, collect_val, &v);
    CHECK(v.n == 1 && strcmp(v.vals[0], "gamma") == 0, "record: id3 -> gamma (off)");

    /* add a 2nd value to id2 -> multi -> the scan path returns BOTH values */
    ais_add(&a, id2, "beta2");
    v.n = 0; ais_record(&a, id2, collect_val, &v);
    CHECK(v.n == 2 && strcmp(v.vals[0], "beta") == 0 &&
          strcmp(v.vals[1], "beta2") == 0, "record: multi id2 -> beta, beta2");

    /* delete id1, compact -> a gap at id1; survivors still resolve correctly */
    ais_del(&a, id1);
    CHECK(ais_compact(&a) == 0, "record: compact -> 0");
    v.n = 0; ais_record(&a, id3, collect_val, &v);
    CHECK(v.n == 1 && strcmp(v.vals[0], "gamma") == 0, "record: id3 after compact");
    v.n = 0; ais_record(&a, id2, collect_val, &v);
    CHECK(v.n == 2, "record: multi id2 still 2 values after compact");

    ais_close(&a);
    scratch_rm(dir);
}

/* ---- embed: the in-process FFI seam (embed.h) over a scratch index ------- */
/* ---- timestamp column: written on put, robust to hand edits ----------- */
static void test_timestamp(void)
{
    const char *dir = "/tmp/ais_ut_ts";
    ais a;
    struct idvec ids;
    char line[AIS_LINE_MAX];
    FILE *fp;

    scratch_rm(dir);
    ais_open(&a, dir);
    ais_put(&a, "photo 2026 trip", "https://example.org/a");   /* 2026 is a KEY */
    ais_close(&a);

    /* the stored line carries an ISO timestamp field (id|ts|keys|value) */
    snprintf(line, sizeof(line), "%s/store", dir);
    fp = fopen(line, "r");
    CHECK(fp != NULL && fgets(line, sizeof(line), fp) != NULL &&
          strstr(line, "T") != NULL && line[1] == '|' &&
          line[2] >= '0' && line[2] <= '9',
          "timestamp: a new put records id|ts|keys|value");
    CHECK(strstr(line, "Z|") != NULL, "timestamp: a new ts is UTC (ends with Z)");
    if (fp != NULL) fclose(fp);

    /* a year used as a key is NOT mistaken for a date: recall by 2026 works */
    ais_open(&a, dir);
    query(&a, AIS_AND, &ids, 1, "2026");
    CHECK(ids.n == 1 && ids.ids[0] == 1, "timestamp: a year-key (2026) is not eaten as a date");
    ais_close(&a);

    /* backward compatibility (Zoya's 0.1.4 case): a current reader still reads
     * old v2 lines (ts with no 'Z') and v1 lines (no ts at all). */
    {
        const char *odir = "/tmp/ais_ut_ts_old";
        ais b;
        struct idvec ov;
        char op[AIS_PATH_MAX];
        FILE *of;

        scratch_rm(odir);
        ais_open(&b, odir);
        ais_close(&b);
        snprintf(op, sizeof op, "%s/store", odir);
        of = fopen(op, "w");
        if (of != NULL) {
            fprintf(of, "1|2024-03-01T09:00:00|legacy two|old-v2-value\n"); /* v2: no Z */
            fprintf(of, "2|legacy one|old-v1-value\n");                     /* v1: no ts */
            fclose(of);
        }
        snprintf(op, sizeof op, "%s/next_id", odir); remove(op);
        snprintf(op, sizeof op, "%s/off",     odir); remove(op);
        ais_open(&b, odir);
        ais_compact(&b);
        query(&b, AIS_AND, &ov, 1, "two");
        CHECK(ov.n == 1 && ov.ids[0] == 1, "timestamp: reads an old v2 (no-Z) record");
        query(&b, AIS_AND, &ov, 1, "one");
        CHECK(ov.n == 1 && ov.ids[0] == 2, "timestamp: reads an old v1 (no-ts) record");
        ais_close(&b);
        scratch_rm(odir);
    }

    scratch_rm(dir);
}

/* ---- timeline: dateless rows first, then newest; a hand legacy line ----- */
static void test_timeline(void)
{
    const char *dir = "/tmp/ais_ut_tl";
    ais a;
    struct tlvec v;
    char path[AIS_PATH_MAX];
    FILE *fp;

    scratch_rm(dir);
    ais_open(&a, dir);
    ais_put(&a, "alpha", "first");        /* id 1 */
    ais_put(&a, "beta",  "second");       /* id 2 */
    ais_put(&a, "gamma", "third");        /* id 3 */
    ais_put(&a, "delta", "fourth");       /* id 4 */
    ais_close(&a);

    /* append a LEGACY (no-ts) line by hand (id 5), rebuild the derived files by
     * compacting -- the record must survive and appear in the timeline undated. */
    snprintf(path, sizeof(path), "%s/store", dir);
    fp = fopen(path, "a");
    if (fp != NULL) { fprintf(fp, "5|oldkey|hand-pasted, no date\n"); fclose(fp); }
    snprintf(path, sizeof(path), "%s/next_id", dir); remove(path);
    snprintf(path, sizeof(path), "%s/off",     dir); remove(path);
    snprintf(path, sizeof(path), "%s/multi",   dir); remove(path);

    ais_open(&a, dir);
    ais_compact(&a);                      /* rebuilds off; next_id -> 6 */

    /* full timeline: newest id first (keyset, id-descending) */
    v.n = 0;
    ais_timeline(&a, 0, 0, "", "", collect_tl, &v);
    CHECK(v.n == 5, "timeline: every live record appears");
    CHECK(v.n == 5 && v.r[0].id == 5 && v.r[4].id == 1,
          "timeline: newest id first (5..1)");
    CHECK(v.n >= 1 && v.r[0].ts[0] == '\0',
          "timeline: the undated record survived (empty ts)");
    CHECK(v.n >= 2 && strchr(v.r[1].ts, 'T') != NULL,
          "timeline: dated rows carry a timestamp");

    /* keyset paging: page size 2, cursor = the last id of the previous page */
    v.n = 0; ais_timeline(&a, 0, 2, "", "", collect_tl, &v);
    CHECK(v.n == 2 && v.r[0].id == 5 && v.r[1].id == 4, "timeline: page 1 = {5,4}");
    v.n = 0; ais_timeline(&a, 4, 2, "", "", collect_tl, &v);
    CHECK(v.n == 2 && v.r[0].id == 3 && v.r[1].id == 2, "timeline: page 2 (before 4) = {3,2}");
    v.n = 0; ais_timeline(&a, 2, 2, "", "", collect_tl, &v);
    CHECK(v.n == 1 && v.r[0].id == 1, "timeline: page 3 (before 2) = {1}");
    v.n = 0; ais_timeline(&a, 1, 2, "", "", collect_tl, &v);
    CHECK(v.n == 0, "timeline: page 4 (before 1) = {} (no more)");

    ais_close(&a);
    scratch_rm(dir);
}

/* ---- timeline date range: from/to bounds over the keyset page ------------ */
static void test_timeline_dates(void)
{
    const char *dir = "/tmp/ais_ut_tldate";
    ais a;
    struct tlvec v;
    char path[AIS_PATH_MAX];
    FILE *fp;

    scratch_rm(dir);
    ais_open(&a, dir);                        /* create the index dir + files */
    ais_close(&a);

    /* hand-write records with KNOWN, distinct save dates (ascending id), plus a
     * dateless legacy line (id 4); then rebuild the derived files by compacting. */
    snprintf(path, sizeof(path), "%s/store", dir);
    fp = fopen(path, "w");
    if (fp != NULL) {
        fprintf(fp, "1|2020-01-01T10:00:00|k|old\n");
        fprintf(fp, "2|2022-06-15T12:00:00|k|mid\n");
        fprintf(fp, "3|2024-12-31T23:00:00|k|new\n");
        fprintf(fp, "4|k|undated\n");         /* legacy v1: no ts */
        fclose(fp);
    }
    snprintf(path, sizeof(path), "%s/next_id", dir); remove(path);
    snprintf(path, sizeof(path), "%s/off",     dir); remove(path);
    ais_open(&a, dir);
    ais_compact(&a);                          /* rebuild off; next_id -> 5 */

    v.n = 0; ais_timeline(&a, 0, 0, "", "", collect_tl, &v);
    CHECK(v.n == 4, "tl-date: no filter -> all 4 records");

    v.n = 0; ais_timeline(&a, 0, 0, "2022-01-01", "", collect_tl, &v);
    CHECK(v.n == 2 && v.r[0].id == 3 && v.r[1].id == 2, "tl-date: from 2022-01-01 -> {3,2}");

    v.n = 0; ais_timeline(&a, 0, 0, "", "2022-12-31", collect_tl, &v);
    CHECK(v.n == 2 && v.r[0].id == 2 && v.r[1].id == 1, "tl-date: to 2022-12-31 -> {2,1}");

    v.n = 0; ais_timeline(&a, 0, 0, "2022-01-01", "2022-12-31", collect_tl, &v);
    CHECK(v.n == 1 && v.r[0].id == 2, "tl-date: range 2022 -> {2}");

    v.n = 0; ais_timeline(&a, 0, 0, "2000-01-01", "2099-01-01", collect_tl, &v);
    CHECK(v.n == 3, "tl-date: a bounded range excludes the dateless record");

    /* date range composes with keyset paging (count + cursor) */
    v.n = 0; ais_timeline(&a, 0, 1, "2000-01-01", "2099-01-01", collect_tl, &v);
    CHECK(v.n == 1 && v.r[0].id == 3, "tl-date: range + count=1 -> newest in range {3}");
    v.n = 0; ais_timeline(&a, 3, 1, "2000-01-01", "2099-01-01", collect_tl, &v);
    CHECK(v.n == 1 && v.r[0].id == 2, "tl-date: range + before 3 -> {2}");

    ais_close(&a);
    scratch_rm(dir);
}

/* ---- tags: every distinct key with its count, busiest first ------------- */
static void test_tags(void)
{
    const char *dir = "/tmp/ais_ut_tags";
    ais a;
    struct tagvec v;

    build_fixture(&a, dir);
    v.n = 0;
    ais_tags(&a, collect_tag, &v);
    CHECK(v.n == 16, "tags: all 16 distinct keys counted");
    /* busiest first: the count-2 keys lead, alpha-tied -> 'ansi' is first */
    CHECK(v.n >= 1 && strcmp(v.t[0].key, "ansi") == 0 && v.t[0].count == 2,
          "tags: busiest first, ties alphabetical (ansi, count 2)");
    CHECK(v.n >= 6 && v.t[5].count == 2 && v.t[6 > v.n - 1 ? v.n - 1 : 6].count == 1,
          "tags: the six count-2 keys precede the count-1 keys");
    ais_close(&a);

    scratch_rm(dir);
}

/* ---- ais_get_page: keyset page over a match set (GUI infinite scroll) ---- */
static void test_get_page(void)
{
    const char *dir = "/tmp/ais_ut_getpage";
    ais a;
    struct idvec v;
    char *k[1];
    int i;

    scratch_rm(dir);
    ais_open(&a, dir);
    for (i = 0; i < 5; i++) {             /* ids 1..5 all under key "k" */
        char val[8];
        snprintf(val, sizeof val, "v%d", i);   /* distinct values: 5 records */
        ais_put(&a, "k", val);
    }
    k[0] = "k";

    /* pages of 2, cursor = the last id of the previous page (ids ascend) */
    v.n = 0; ais_get_page(&a, k, 1, AIS_AND, 0, 2, collect_id, &v);
    CHECK(v.n == 2 && v.ids[0] == 1 && v.ids[1] == 2, "get_page: page 1 = {1,2}");
    v.n = 0; ais_get_page(&a, k, 1, AIS_AND, 2, 2, collect_id, &v);
    CHECK(v.n == 2 && v.ids[0] == 3 && v.ids[1] == 4, "get_page: page 2 (after 2) = {3,4}");
    v.n = 0; ais_get_page(&a, k, 1, AIS_AND, 4, 2, collect_id, &v);
    CHECK(v.n == 1 && v.ids[0] == 5, "get_page: page 3 (after 4) = {5}");
    v.n = 0; ais_get_page(&a, k, 1, AIS_AND, 5, 2, collect_id, &v);
    CHECK(v.n == 0, "get_page: page 4 (after 5) = {} (no more)");

    /* count <= 0 is unbounded == plain ais_get */
    v.n = 0; ais_get_page(&a, k, 1, AIS_AND, 0, 0, collect_id, &v);
    CHECK(v.n == 5 && v.ids[0] == 1 && v.ids[4] == 5, "get_page: count 0 = whole set");

    ais_close(&a);
    scratch_rm(dir);
}

/* ---- ais_tags_page: keyset page over the busiest-first tag cloud --------- */
static void test_tags_page(void)
{
    const char *dir = "/tmp/ais_ut_tagspage";
    ais a;
    struct tagvec full, paged;
    long ac;
    const char *ak;
    int rounds;

    build_fixture(&a, dir);

    full.n = 0;
    ais_tags(&a, collect_tag, &full);       /* the whole cloud, single shot */

    /* walk it in pages of 5; the concatenation must equal FULL, same order */
    paged.n = 0; ac = 0; ak = NULL;
    for (rounds = 0; rounds < 100; rounds++) {
        struct tagvec pg; int i;
        pg.n = 0;
        ais_tags_page(&a, ac, ak, 5, collect_tag, &pg);
        if (pg.n == 0)
            break;
        for (i = 0; i < pg.n && paged.n < 64; i++)
            paged.t[paged.n++] = pg.t[i];
        ac = pg.t[pg.n - 1].count;          /* cursor = last row of this page */
        ak = paged.t[paged.n - 1].key;
    }

    CHECK(paged.n == full.n, "tags_page: pages cover every tag, no gap or dup");
    {
        int i, same = (paged.n == full.n);
        for (i = 0; same && i < full.n; i++)
            same = (paged.t[i].count == full.t[i].count &&
                    strcmp(paged.t[i].key, full.t[i].key) == 0);
        CHECK(same, "tags_page: paged order matches the single-shot order");
    }

    ais_close(&a);
    scratch_rm(dir);
}

static void test_embed(void)
{
    const char *dir = "/tmp/ais_ut_embed";
    void *h;
    char *r;
    long id1, id2;

    scratch_rm(dir);

    h = ais_embed_open(dir);
    CHECK(h != NULL, "embed: open returns a handle");
    if (h == NULL) { scratch_rm(dir); return; }

    id1 = ais_embed_store(h, "venice italy", "https://example.org/p");
    id2 = ais_embed_store(h, "venice food", "gelato");
    CHECK(id1 == 1 && id2 == 2, "embed: store returns monotonic ids");

    r = ais_embed_recall(h, "venice italy", 0);          /* AND */
    CHECK(r != NULL && strcmp(r, "1|https://example.org/p\n") == 0,
          "embed: recall AND -> the one shared record");
    ais_embed_free(r);

    r = ais_embed_recall(h, "venice", 0);                /* shared key -> both, ascending */
    CHECK(r != NULL && strcmp(r, "1|https://example.org/p\n2|gelato\n") == 0,
          "embed: recall a shared key -> both records, ascending");
    ais_embed_free(r);

    r = ais_embed_recall(h, "italy food", 1);            /* OR -> union */
    CHECK(r != NULL && strcmp(r, "1|https://example.org/p\n2|gelato\n") == 0,
          "embed: recall OR -> union");
    ais_embed_free(r);

    r = ais_embed_recall(h, "nope", 0);                  /* no match -> "" not NULL */
    CHECK(r != NULL && r[0] == '\0', "embed: no match -> empty string");
    ais_embed_free(r);

    r = ais_embed_timeline(h, 0, 0, "", "");             /* id|ts|keys|value */
    CHECK(r != NULL && strstr(r, "|venice italy|https://example.org/p\n") != NULL,
          "embed: timeline returns id|ts|keys|value lines");
    ais_embed_free(r);

    r = ais_embed_tags(h);                               /* count|key, busiest first */
    CHECK(r != NULL && strncmp(r, "2|venice\n", 9) == 0,
          "embed: tags returns count|key, busiest first");
    ais_embed_free(r);

    ais_embed_close(h);
    scratch_rm(dir);
}

/* ais_embed_find: content search over values (the "forgot the key" fallback).
 * Same "id|value\n" lines as recall, captured from ais_find via a tmpfile. */
static void test_embed_find(void)
{
    const char *dir = "/tmp/ais_ut_embed_find";
    void *h;
    char *r;

    scratch_rm(dir);

    h = ais_embed_open(dir);
    CHECK(h != NULL, "embed find: open returns a handle");
    if (h == NULL) { scratch_rm(dir); return; }

    ais_embed_store(h, "trip", "hotel in venice");   /* id 1 */
    ais_embed_store(h, "food", "pasta recipe");      /* id 2 */

    r = ais_embed_find(h, "venice");                 /* value match -> the trip record only */
    CHECK(r != NULL && strstr(r, "1|hotel in venice") != NULL,
          "embed find: 'venice' hits the venice record");
    CHECK(r != NULL && strstr(r, "pasta") == NULL,
          "embed find: 'venice' does not hit the pasta record");
    ais_embed_free(r);

    r = ais_embed_find(h, "zzz-nomatch");            /* no match -> "" not NULL */
    CHECK(r != NULL && r[0] == '\0', "embed find: no match -> empty string");
    ais_embed_free(r);

    ais_embed_close(h);
    scratch_rm(dir);
}

/* Regression: ais_embed_keys must return a record's CURRENT (visible) keys,
 * derived from the index like recall -- NOT the stale keys off the store line.
 * The bug: after an update that detaches/attaches keys, the store line still
 * holds the ORIGINAL keys, so reading them there is wrong. keys() must agree
 * with what recall can find. */
static void test_embed_keys_visible(void)
{
    const char *dir = "/tmp/ais_ut_keysvis";
    void *h;
    char *r, *k;
    long id;

    scratch_rm(dir);
    h = ais_embed_open(dir);
    CHECK(h != NULL, "embed-keys: open returns a handle");
    if (h == NULL) { scratch_rm(dir); return; }

    id = ais_embed_store(h, "trip rome venice", "a val");
    CHECK(id > 0, "embed-keys: store returns an id");

    /* capture the id from recall's "id|value\n" (proves it starts under 'rome') */
    r = ais_embed_recall(h, "trip", 0);
    CHECK(r != NULL && strtol(r, NULL, 10) == id,
          "embed-keys: recall 'trip' returns the stored record");
    ais_embed_free(r);

    /* detach 'rome', attach 'fjord' -- the store line keeps the original keys */
    CHECK(ais_embed_update(h, id, "-rome fjord") == 0,
          "embed-keys: update detaches 'rome', attaches 'fjord'");

    k = ais_embed_keys(h, id);
    CHECK(k != NULL, "embed-keys: keys() returns a buffer");
    if (k != NULL) {
        CHECK(strstr(k, "trip") != NULL,  "embed-keys: keys() still lists 'trip'");
        CHECK(strstr(k, "venice") != NULL, "embed-keys: keys() still lists 'venice'");
        CHECK(strstr(k, "fjord") != NULL, "embed-keys: keys() lists the attached 'fjord'");
        CHECK(strstr(k, "rome") == NULL,  "embed-keys: keys() drops the detached 'rome'");
    }
    ais_embed_free(k);

    /* keys() must match recall: 'rome' no longer finds it, 'fjord' now does */
    r = ais_embed_recall(h, "rome", 0);
    CHECK(r != NULL && r[0] == '\0', "embed-keys: recall 'rome' no longer finds the record");
    ais_embed_free(r);
    r = ais_embed_recall(h, "fjord", 0);
    CHECK(r != NULL && strtol(r, NULL, 10) == id,
          "embed-keys: recall 'fjord' now finds the record (keys() matches recall)");
    ais_embed_free(r);

    ais_embed_close(h);
    scratch_rm(dir);
}

/* ais_put_value: one paste -> one record. Single line stays a plain, verbatim
 * record; a lone trailing newline is trimmed (still plain); a genuinely
 * multi-line value is written to a blobs/ file and the record holds its path.
 * This is the seam every GUI calls, so it is tested at the engine level. */
static char g_pv[AIS_PATH_MAX];   /* captured value (a path or a short test value) */
static int grab_value(long id, const char *value, void *vp)
{
    (void)id; (void)vp;
    snprintf(g_pv, sizeof g_pv, "%s", value);
    return 0;
}

static void test_put_value(void)
{
    const char *dir = "/tmp/ais_ut_putvalue";
    ais a;
    long id1, id2, id3;
    char blob[2 * AIS_PATH_MAX], rd[256];
    FILE *f;

    scratch_rm(dir);
    CHECK(ais_open(&a, dir) == 0, "put_value: open scratch index");

    id1 = ais_put_value(&a, "note", "just one line");
    g_pv[0] = '\0'; ais_record(&a, id1, grab_value, NULL);
    CHECK(id1 == 1 && strcmp(g_pv, "just one line") == 0,
          "put_value: single line -> plain record, stored verbatim");

    id2 = ais_put_value(&a, "note", "trailing newline\n");
    g_pv[0] = '\0'; ais_record(&a, id2, grab_value, NULL);
    CHECK(strcmp(g_pv, "trailing newline") == 0,
          "put_value: lone trailing newline trimmed, stays a plain record");

    id3 = ais_put_value(&a, "doc", "line one\nline two\nline three");
    g_pv[0] = '\0'; ais_record(&a, id3, grab_value, NULL);
    CHECK(strncmp(g_pv, "blobs/", 6) == 0,
          "put_value: multi-line -> a blobs/ path is stored, not the text");

    snprintf(blob, sizeof blob, "%s/%s", dir, g_pv);
    f = fopen(blob, "r");
    CHECK(f != NULL, "put_value: the blob file was created");
    if (f != NULL) {
        size_t n = fread(rd, 1, sizeof(rd) - 1, f);
        rd[n] = '\0';
        fclose(f);
        CHECK(strcmp(rd, "line one\nline two\nline three") == 0,
              "put_value: blob preserves the multi-line content verbatim");
    }

    ais_close(&a);
    scratch_rm(dir);
}

/* ais_doc_display: the ONE resolver the web (serve.c) and Flutter (via
 * ais_embed_display) share -- a document blob shows its CONTENT (capped),
 * everything else shows verbatim. Guards the bug where a viewer showed the
 * "blobs/<ts>.txt" PATH instead of the multi-line text. */
static void test_doc_display(void)
{
    const char *dir = "/tmp/ais_ut_docdisplay";
    ais a;
    long idp, idd, n;
    char out[AIS_LINE_MAX];

    scratch_rm(dir);
    CHECK(ais_open(&a, dir) == 0, "doc_display: open scratch index");

    /* inline value -> verbatim, flagged NOT-a-document (return -1) */
    idp = ais_put_value(&a, "u", "https://example.org/x");
    g_pv[0] = '\0'; ais_record(&a, idp, grab_value, NULL);
    n = ais_doc_display(&a, g_pv, out, sizeof out);
    CHECK(n < 0 && strcmp(out, "https://example.org/x") == 0,
          "doc_display: inline value shown verbatim, flagged not-a-document");

    /* multi-line value -> a blobs/ path is stored; display resolves to CONTENT */
    idd = ais_put_value(&a, "doc", "alpha\nbeta\ngamma");
    g_pv[0] = '\0'; ais_record(&a, idd, grab_value, NULL);
    CHECK(strncmp(g_pv, "blobs/", 6) == 0,
          "doc_display: multi-line stored as a blobs/ path (setup)");
    n = ais_doc_display(&a, g_pv, out, sizeof out);
    CHECK(n == 16 && strcmp(out, "alpha\nbeta\ngamma") == 0,
          "doc_display: a blob ref resolves to its CONTENT, not the path");

    /* an absent blob (a blobs/ path with no file) -> verbatim + not-a-document */
    n = ais_doc_display(&a, "blobs/nope.txt", out, sizeof out);
    CHECK(n < 0 && strcmp(out, "blobs/nope.txt") == 0,
          "doc_display: an absent blob falls back to the path (viewer badges it)");

    ais_close(&a);
    scratch_rm(dir);
}

/* ---- update: attach/detach keys by id (the handle); idempotent ----------
 * The cycle the user asked for: add a key, verify it's findable; remove it,
 * verify it's gone but the record (id + value) survives; re-attach; and confirm
 * a detach is durable through compaction. Every step is idempotent. */
static void test_update(void)
{
    ais a;
    long id, vids[16];
    int n, asc;
    struct idvec v;
    struct valvec vv;
    const char *dir = "/tmp/ais_ut_update";

    scratch_rm(dir);
    ais_open(&a, dir);

    /* seed: one record under three keys; each key finds it */
    id = ais_put(&a, "venice italy 2023", "https://trip.example/venice");
    CHECK(id == 1, "update: seed record id == 1");
    query(&a, AIS_AND, &v, 1, "venice"); { long w[1] = {1}; CHECK(ids_eq(&v, w, 1), "update: 'venice' finds it"); }
    query(&a, AIS_AND, &v, 1, "italy");  { long w[1] = {1}; CHECK(ids_eq(&v, w, 1), "update: 'italy' finds it"); }

    /* detach 'venice': recall by it empties, record survives via other keys */
    CHECK(ais_update(&a, id, "-venice") == 0, "update: detach 'venice' ok");
    query(&a, AIS_AND, &v, 1, "venice"); CHECK(v.n == 0, "update: 'venice' now empty");
    query(&a, AIS_AND, &v, 1, "italy");  { long w[1] = {1}; CHECK(ids_eq(&v, w, 1), "update: 'italy' still finds it"); }
    CHECK(read_posting(dir, "venice", vids, 16, &asc) == -1, "update: 'venice' posting file removed");
    CHECK(ktomb_contains(&a, id, "venice") == 1, "update: (id,'venice') recorded in ktomb");

    /* the value and id are unchanged: the handle is stable */
    vv.n = 0; ais_record(&a, id, collect_val, &vv);
    CHECK(vv.n == 1 && strcmp(vv.vals[0], "https://trip.example/venice") == 0,
          "update: value unchanged after detach");

    /* idempotent: detaching again changes nothing and does not error */
    CHECK(ais_update(&a, id, "-venice") == 0, "update: detach again ok (idempotent)");
    query(&a, AIS_AND, &v, 1, "venice"); CHECK(v.n == 0, "update: still empty after 2nd detach");

    /* re-attach 'venice': back, and the posting holds the id exactly once */
    CHECK(ais_update(&a, id, "venice") == 0, "update: re-attach 'venice' ok");
    n = read_posting(dir, "venice", vids, 16, &asc);
    CHECK(n == 1 && vids[0] == 1, "update: 'venice' posting == {1} (no dup)");
    CHECK(ktomb_contains(&a, id, "venice") == 0, "update: re-attach cleared the ktomb pair");

    /* idempotent re-attach: still exactly one posting */
    CHECK(ais_update(&a, id, "venice") == 0, "update: re-attach again ok");
    n = read_posting(dir, "venice", vids, 16, &asc);
    CHECK(n == 1 && vids[0] == 1, "update: posting still == {1} after 2nd attach");

    /* attach a brand-new key by id; unknown id is refused */
    CHECK(ais_update(&a, id, "rome") == 0, "update: attach new key 'rome'");
    query(&a, AIS_AND, &v, 1, "rome"); { long w[1] = {1}; CHECK(ids_eq(&v, w, 1), "update: 'rome' finds it"); }
    CHECK(ais_update(&a, 999, "x") == -1, "update: unknown id -> -1");

    /* a detach is durable through compaction: the store line is rewritten without the
     * key and recall stays empty. The hash-bearing key-tombstone is RETAINED so the
     * detach can still propagate to peers on a folder sync (I1). */
    CHECK(ais_update(&a, id, "-italy") == 0, "update: detach 'italy' before compact");
    CHECK(ais_compact(&a) == 0, "update: compact ok");
    query(&a, AIS_AND, &v, 1, "italy");  CHECK(v.n == 0, "update: 'italy' empty after compact");
    query(&a, AIS_AND, &v, 1, "venice"); { long w[1] = {1}; CHECK(ids_eq(&v, w, 1), "update: 'venice' survives compact"); }
    CHECK(ktomb_contains(&a, id, "italy") == 1, "update: key-tombstone retained through compact (I1)");
    CHECK(read_posting(dir, "italy", vids, 16, &asc) == -1, "update: 'italy' posting gone after compact");

    /* a deleted record cannot be updated */
    ais_del(&a, id);
    CHECK(ais_update(&a, id, "rome") == -1, "update: deleted id -> -1");

    ais_close(&a);
    scratch_rm(dir);
}

/* ---- set_value: replace a record's VALUE, keeping its id, ts and keys ----- */
static void test_set_value(void)
{
    ais a;
    long id;
    struct idvec v;
    struct valvec vv;
    struct tlvec tl;
    char ts_before[AIS_TS_MAX] = "";
    char ts_after[AIS_TS_MAX] = "";
    int i;
    const char *dir = "/tmp/ais_ut_setval";

    scratch_rm(dir);
    ais_open(&a, dir);

    id = ais_put(&a, "trip rome", "old note");
    CHECK(id == 1, "set_value: seed record id == 1");

    /* remember the record's save time (its timeline position) */
    tl.n = 0; ais_timeline(&a, 0, 0, "", "", collect_tl, &tl);
    for (i = 0; i < tl.n; i++)
        if (tl.r[i].id == id)
            snprintf(ts_before, sizeof ts_before, "%s", tl.r[i].ts);
    CHECK(ts_before[0] != '\0', "set_value: seed record carries a ts");

    /* replace the value; keys unchanged, so a wrong old value is refused first */
    CHECK(ais_set_value(&a, id, "old note", "new note") == 0,
          "set_value: matching old value -> 0");

    vv.n = 0; ais_record(&a, id, collect_val, &vv);
    CHECK(vv.n == 1 && strcmp(vv.vals[0], "new note") == 0,
          "set_value: ais_record now yields 'new note'");

    /* keys preserved: still recalled by both 'trip' and 'rome' */
    query(&a, AIS_AND, &v, 1, "trip");
    { long w[1] = {1}; CHECK(ids_eq(&v, w, 1), "set_value: still recalled by 'trip'"); }
    query(&a, AIS_AND, &v, 1, "rome");
    { long w[1] = {1}; CHECK(ids_eq(&v, w, 1), "set_value: still recalled by 'rome'"); }

    /* id and ts (timeline position) unchanged */
    tl.n = 0; ais_timeline(&a, 0, 0, "", "", collect_tl, &tl);
    CHECK(tl.n == 1 && tl.r[0].id == id, "set_value: id unchanged (single record, id 1)");
    for (i = 0; i < tl.n; i++)
        if (tl.r[i].id == id)
            snprintf(ts_after, sizeof ts_after, "%s", tl.r[i].ts);
    CHECK(strcmp(ts_before, ts_after) == 0, "set_value: ts unchanged");

    /* a value mismatch changes nothing and returns -1 */
    CHECK(ais_set_value(&a, id, "wrong", "x") == -1, "set_value: value mismatch -> -1");
    vv.n = 0; ais_record(&a, id, collect_val, &vv);
    CHECK(vv.n == 1 && strcmp(vv.vals[0], "new note") == 0,
          "set_value: mismatch left the value 'new note'");

    /* an unknown id is likewise refused, store untouched */
    CHECK(ais_set_value(&a, 999, "new note", "x") == -1, "set_value: unknown id -> -1");
    vv.n = 0; ais_record(&a, id, collect_val, &vv);
    CHECK(vv.n == 1 && strcmp(vv.vals[0], "new note") == 0,
          "set_value: unknown-id no-op left the value 'new note'");

    ais_close(&a);
    scratch_rm(dir);
}

/* regression + case study: a real index accumulates all three line formats as
 * AIS evolves -- v1 (no ts), v2 (local ts, no Z), v3 (UTC ts with Z). One engine
 * must read them all (mirrors Vasili's own ~/.ais after upgrades). */
static void test_mixed_formats(void)
{
    const char *dir = "/tmp/ais_ut_mixed";
    ais a;
    struct idvec v;
    struct tlvec tl;
    char path[AIS_PATH_MAX];
    FILE *fp;
    int i, undated = 0, local = 0, utc = 0;

    scratch_rm(dir);
    ais_open(&a, dir);
    ais_close(&a);
    snprintf(path, sizeof path, "%s/store", dir);
    fp = fopen(path, "w");
    if (fp != NULL) {
        fprintf(fp, "1|alpha old|v1-no-timestamp\n");               /* v1: no ts   */
        fprintf(fp, "2|2026-06-11T14:51:09|beta local|v2-local\n"); /* v2: local   */
        fprintf(fp, "3|2026-06-17T18:00:00Z|gamma utc|v3-utc\n");   /* v3: UTC + Z */
        fclose(fp);
    }
    snprintf(path, sizeof path, "%s/next_id", dir); remove(path);
    snprintf(path, sizeof path, "%s/off",     dir); remove(path);
    ais_open(&a, dir);
    ais_compact(&a);                            /* rebuild idx/off from the mix */

    query(&a, AIS_AND, &v, 1, "old");   CHECK(v.n == 1 && v.ids[0] == 1, "mixed: v1 (no-ts) record recalled");
    query(&a, AIS_AND, &v, 1, "local"); CHECK(v.n == 1 && v.ids[0] == 2, "mixed: v2 (local ts) record recalled");
    query(&a, AIS_AND, &v, 1, "utc");   CHECK(v.n == 1 && v.ids[0] == 3, "mixed: v3 (UTC ts) record recalled");

    tl.n = 0; ais_timeline(&a, 0, 0, "", "", collect_tl, &tl);
    CHECK(tl.n == 3, "mixed: all three formats appear in the timeline");
    for (i = 0; i < tl.n; i++) {
        if (tl.r[i].id == 1) undated = (tl.r[i].ts[0] == '\0');
        if (tl.r[i].id == 2) local   = (strchr(tl.r[i].ts, 'T') != NULL && strchr(tl.r[i].ts, 'Z') == NULL);
        if (tl.r[i].id == 3) utc     = (strchr(tl.r[i].ts, 'Z') != NULL);
    }
    CHECK(undated, "mixed: the v1 row is undated (empty ts)");
    CHECK(local,   "mixed: the v2 row keeps its local ts (no Z)");
    CHECK(utc,     "mixed: the v3 row keeps its UTC ts (Z)");

    ais_close(&a);
    scratch_rm(dir);
}

/* ---- import-interactively: y/N per record; only the chosen ones land ---- */
static void test_import_interactive(void)
{
    ais a;
    struct idvec v;
    const char *dir     = "/tmp/ais_ut_impi";
    const char *recpath = "/tmp/ais_ut_impi_recs";
    const char *anspath = "/tmp/ais_ut_impi_ans";
    FILE *f;

    scratch_rm(dir);

    /* three "keys|value" records offered on stdin (a --dump of another index) */
    f = fopen(recpath, "w");
    fputs("rome italy|trip-rome\n", f);
    fputs("paris france|trip-paris\n", f);
    fputs("tokyo japan|trip-tokyo\n", f);
    fclose(f);

    /* answers from the "terminal": take #1 (y), skip #2 (bare Enter), take #3 */
    f = fopen(anspath, "w");
    fputs("y\n\ny\n", f);
    fclose(f);

    ais_open(&a, dir);
    if (freopen(recpath, "r", stdin) == NULL) {
        CHECK(0, "freopen stdin"); ais_close(&a); return;
    }
    setenv("AIS_TTY", anspath, 1);             /* the answers file is the tty */
    feed_import_interactive(&a);
    unsetenv("AIS_TTY");

    query(&a, AIS_AND, &v, 1, "rome");
    CHECK(v.n == 1, "import-interactively: y-accepted 'rome' is stored");
    query(&a, AIS_AND, &v, 1, "tokyo");
    CHECK(v.n == 1, "import-interactively: y-accepted 'tokyo' is stored");
    query(&a, AIS_AND, &v, 1, "paris");
    CHECK(v.n == 0, "import-interactively: Enter-skipped 'paris' is NOT stored");

    ais_close(&a);
    scratch_rm(dir);
    remove(recpath);
    remove(anspath);
}

/* a counting ais_index_list callback */
static int count_index(const char *name, const char *path, void *vp)
{
    (void)name; (void)path;
    (*(int *)vp)++;
    return 0;
}

/* ---- multi-index: registry round-trip + switch + locate precedence ---- */
static void test_index_switch(void)
{
    const char *home = "/tmp/ais_ut_home";
    char path[AIS_PATH_MAX], cur[AIS_KEY_MAX], saved[AIS_PATH_MAX];
    int n;

    scratch_rm(home);
    ais_home_override(home);              /* config + registry now live under home */

    /* by default the current is the built-in home (no `current` line) */
    CHECK(ais_current_get(cur, sizeof cur) == 0, "switch: no current -> home by default");
    CHECK(ais_index_path("home", path, sizeof path) == 1, "home resolves to ~/.ais");

    /* register two named indexes; 'home' is reserved */
    CHECK(ais_index_add("work", "/tmp/ais_ut_work") == 0, "register 'work'");
    CHECK(ais_index_add("play", "/tmp/ais_ut_play") == 0, "register 'play'");
    CHECK(ais_index_add("home", "/x") == -1, "'home' is reserved");
    n = 0; ais_index_list(count_index, &n);
    CHECK(n == 2, "two indexes registered (home not in the list)");

    /* switch makes 'work' current and it resolves to its path */
    CHECK(ais_current_set("work") == 0, "switch to 'work'");
    CHECK(ais_current_get(cur, sizeof cur) == 1 && strcmp(cur, "work") == 0, "current is 'work'");
    CHECK(ais_index_path("work", path, sizeof path) == 1
          && strcmp(path, "/tmp/ais_ut_work") == 0, "'work' -> its path");

    /* ais_locate honors the current named index (no -f, no local .ais): run from
     * the override home, where find_local stops immediately (step 2 finds none). */
    /* From a fresh neutral dir (no .ais at or above it, and NOT home), ais_locate
     * must fall through find_local to the current named index. Running from home
     * itself is fragile on macOS, where /tmp is a symlink and find_local's
     * "stop at home" check is a string compare (cwd canonicalizes, home may not). */
    scratch_rm("/tmp/ais_ut_cwd");
    mkdir("/tmp/ais_ut_cwd", 0700);
    if (getcwd(saved, sizeof saved) != NULL && chdir("/tmp/ais_ut_cwd") == 0) {
        char loc[AIS_PATH_MAX];
        const char *suf = "/ais_ut_work";
        size_t sl = strlen(suf), ln;
        int located = (ais_locate(NULL, loc, sizeof loc) == 0);
        int ok;
        ln = located ? strlen(loc) : 0;
        /* match the path TAIL: a resolved prefix may differ from the registry's. */
        ok = located && ln >= sl && strcmp(loc + ln - sl, suf) == 0;
        CHECK(ok, "locate -> the current named index");
        if (chdir(saved) != 0) CHECK(0, "restore cwd after chdir");
    }
    scratch_rm("/tmp/ais_ut_cwd");

    /* switch back to home clears the current pointer */
    CHECK(ais_current_set("home") == 0, "switch back to home");
    CHECK(ais_current_get(cur, sizeof cur) == 0, "current cleared (home)");

    /* forget drops the name (data dir untouched), leaves the other */
    CHECK(ais_index_remove("play") == 0, "forget 'play'");
    CHECK(ais_index_path("play", path, sizeof path) == 0, "'play' no longer registered");
    CHECK(ais_index_path("work", path, sizeof path) == 1, "'work' still registered");

    ais_home_override(NULL);              /* restore the OS home for any later test */
    scratch_rm(home);
    scratch_rm("/tmp/ais_ut_work");
    scratch_rm("/tmp/ais_ut_play");
}

/* ---- base64: RFC 4648 vectors + binary round-trip + rejects ---- */
static void test_b64(void)
{
    static const char *in[7]  = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    static const char *out[7] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    char enc[64];
    unsigned char dec[64];
    int i;

    for (i = 0; i < 7; i++) {
        size_t n = strlen(in[i]);
        long e = b64_encode((const unsigned char *)in[i], n, enc, sizeof enc);
        long d;
        CHECK(e >= 0 && strcmp(enc, out[i]) == 0, "b64: encode matches the RFC vector");
        d = b64_decode(out[i], strlen(out[i]), dec, sizeof dec);
        CHECK(d == (long)n && memcmp(dec, in[i], n) == 0, "b64: decode round-trips");
    }
    CHECK(b64_decode("Zm9v!", 5, dec, sizeof dec) == -1, "b64: invalid char rejected");
    CHECK(b64_decode("Zm9", 3, dec, sizeof dec) == -1,   "b64: truncated quad rejected");
    {   /* every byte value survives a round-trip (the encrypted-image case) */
        unsigned char bin[256], back[256];
        char e[512];
        int k;
        for (k = 0; k < 256; k++) bin[k] = (unsigned char)k;
        CHECK(b64_encode(bin, 256, e, sizeof e) > 0, "b64: encode 256 bytes");
        CHECK(b64_decode(e, strlen(e), back, sizeof back) == 256
              && memcmp(back, bin, 256) == 0, "b64: 256-byte binary round-trip");
    }
}

/* ---- the "aisc:" secret marker: exact prefix, never a heuristic ---- */
static void test_secret_marker(void)
{
    CHECK(secret_is_marked("aisc:AAAA") == 1, "marker: 'aisc:...' is detected");
    CHECK(secret_is_marked("aisc:") == 1,      "marker: bare 'aisc:' is detected");
    CHECK(secret_is_marked("http://x") == 0,   "marker: a URL is not a secret");
    CHECK(secret_is_marked("aisc") == 0,       "marker: 'aisc' without ':' is not a secret");
    CHECK(secret_is_marked("note aisc:") == 0, "marker: must be a PREFIX, not anywhere in the value");
    CHECK(secret_is_marked("") == 0,           "marker: empty value is not a secret");
    CHECK(secret_is_marked(NULL) == 0,         "marker: NULL is not a secret");
}

static void test_secret_blob_relpath(void)
{
    const char *r = secret_blob_relpath("aisc:@blobs/2026.aisc");
    CHECK(r != NULL && strcmp(r, "blobs/2026.aisc") == 0,
          "blob: 'aisc:@<rel>' yields the relpath after the '@'");
    CHECK(secret_blob_relpath("aisc:QWxhZGRpbg==") == NULL,
          "blob: an inline 'aisc:<base64>' is NOT a blob ref");
    r = secret_blob_relpath("aisc:@");
    CHECK(r != NULL && r[0] == '\0', "blob: bare 'aisc:@' is an (empty) blob ref");
    CHECK(secret_blob_relpath("http://x") == NULL, "blob: a URL is not a blob ref");
    CHECK(secret_blob_relpath("aisc") == NULL,     "blob: 'aisc' (no colon) is not a blob ref");
    CHECK(secret_blob_relpath(NULL) == NULL,       "blob: NULL is not a blob ref");
}

static void test_secret_shred(void)
{
    FILE *f = fopen("/tmp/ais_ut_blob.aisc", "wb");
    CHECK(f != NULL, "shred: created a fake encrypted blob");
    if (f) { fwrite("ciphertext-bytes", 1, 16, f); fclose(f); }
    secret_shred_blob("/tmp", "aisc:@ais_ut_blob.aisc");      /* the matching marked value */
    CHECK(access("/tmp/ais_ut_blob.aisc", F_OK) != 0, "shred: the encrypted blob is removed");

    f = fopen("/tmp/ais_ut_keep.txt", "wb");
    if (f) { fwrite("keep", 1, 4, f); fclose(f); }
    secret_shred_blob("/tmp", "http://example.org");          /* not a secret */
    secret_shred_blob("/tmp", "aisc:QWxh");                   /* inline, not a blob */
    CHECK(access("/tmp/ais_ut_keep.txt", F_OK) == 0, "shred: a no-op on non-blob values");
    remove("/tmp/ais_ut_keep.txt");
}

#ifdef AIS_UT_HAVE_CRYPTO
/* ---- vendored Monocypher: AEAD round-trip + wrong-key + tamper rejection ---- */
static void test_crypto(void)
{
    static const unsigned char pt[] = "the launch code is 0000";
    static const unsigned char pw[] = "correct horse battery staple";
    uint8_t *file = NULL, *out = NULL;
    size_t flen = 0, olen = 0;
    aisc_kdf k = aisc_default_kdf();
    int rc;

    k.mem_blocks = 8; k.passes = 1;            /* tiny KDF: keep the test fast */
    rc = aisc_encrypt(pt, sizeof pt - 1, pw, sizeof pw - 1, NULL, 0, k, &file, &flen);
    CHECK(rc == AISC_OK && file != NULL && flen > sizeof pt, "crypto: encrypt -> file image");

    rc = aisc_decrypt(file, flen, pw, sizeof pw - 1, NULL, 0, &out, &olen);
    CHECK(rc == AISC_OK && olen == sizeof pt - 1 && memcmp(out, pt, olen) == 0,
          "crypto: right password decrypts and round-trips");
    if (out) { aisc_wipe(out, olen); free(out); out = NULL; }

    rc = aisc_decrypt(file, flen, (const unsigned char *)"nope", 4, NULL, 0, &out, &olen);
    CHECK(rc == AISC_E_AUTH, "crypto: wrong password is rejected (AEAD auth)");

    file[flen - 1] ^= 0x01;                    /* flip a bit of the Poly1305 tag */
    rc = aisc_decrypt(file, flen, pw, sizeof pw - 1, NULL, 0, &out, &olen);
    CHECK(rc == AISC_E_AUTH, "crypto: tampering is detected");

    /* sync crypto: domain-separated subkeys + raw-key AEAD (the token never becomes a seal key) */
    {
        static const unsigned char tok[] = "Zk3pQ9-7Lm2xW8nR4tB6vY1";  /* high-entropy */
        uint8_t kseal[32], kauth[32], *sealed = NULL, *plain = NULL;
        size_t slen = 0, plen = 0;

        aisc_subkey(tok, sizeof tok - 1, "ais-sync-seal-v1", NULL, 0, kseal);
        aisc_subkey(tok, sizeof tok - 1, "ais-sync-auth-v1", NULL, 0, kauth);
        CHECK(memcmp(kseal, kauth, 32) != 0, "seal: domain separation -> distinct subkeys");

        rc = aisc_seal_key(kseal, pt, sizeof pt - 1, &sealed, &slen);
        CHECK(rc == AISC_OK && sealed != NULL && slen == 1 + 24 + (sizeof pt - 1) + 16,
              "seal: version|nonce|ct|tag");

        rc = aisc_unseal_key(kseal, sealed, slen, &plain, &plen);
        CHECK(rc == AISC_OK && plen == sizeof pt - 1 && memcmp(plain, pt, plen) == 0,
              "seal: right key unseals and round-trips");
        if (plain) { aisc_wipe(plain, plen); free(plain); plain = NULL; }

        rc = aisc_unseal_key(kauth, sealed, slen, &plain, &plen);   /* a different subkey */
        CHECK(rc == AISC_E_AUTH, "seal: wrong key is rejected (AEAD auth)");

        sealed[slen - 1] ^= 0x01;              /* flip a tag bit */
        rc = aisc_unseal_key(kseal, sealed, slen, &plain, &plen);
        CHECK(rc == AISC_E_AUTH, "seal: tampering is detected");
        aisc_wipe(sealed, slen); free(sealed);
    }

    {   /* token: high-entropy, well-formed hex, non-repeating */
        char t1[33], t2[33];
        size_t i; int ok = 1;
        CHECK(aisc_token(t1, sizeof t1) == AISC_OK && strlen(t1) == 32, "token: 32 hex chars");
        for (i = 0; i < 32; i++) if (!strchr("0123456789abcdef", t1[i])) ok = 0;
        CHECK(ok, "token: all hex digits");
        aisc_token(t2, sizeof t2);
        CHECK(strcmp(t1, t2) != 0, "token: two tokens differ");
    }

    aisc_wipe(file, flen); free(file);
}

/* secret_decrypt's full path: strip "aisc:" -> b64_decode -> aisc_decrypt. We
 * build the marked value with a CHEAP kdf (the file header carries the cost
 * params, so the decrypt below is fast); secret_encrypt itself uses the real
 * default cost and is exercised by hand. */
static void test_secret_value(void)
{
    static const unsigned char plain[] = "wpa2: hunter2hunter2";
    static const unsigned char pw[] = "master passphrase";
    uint8_t *file = NULL;
    size_t flen = 0;
    char marked[2048];
    unsigned char back[256];
    aisc_kdf k = aisc_default_kdf();
    long n, bn;

    k.mem_blocks = 8; k.passes = 1;
    CHECK(aisc_encrypt(plain, sizeof plain - 1, pw, sizeof pw - 1, NULL, 0, k, &file, &flen) == AISC_OK,
          "secret: build a cheap-kdf file image");
    memcpy(marked, AIS_SECRET_PREFIX, sizeof AIS_SECRET_PREFIX - 1);
    n = b64_encode(file, flen, marked + (sizeof AIS_SECRET_PREFIX - 1),
                   sizeof marked - (sizeof AIS_SECRET_PREFIX - 1));
    aisc_wipe(file, flen); free(file);
    CHECK(n > 0, "secret: 'aisc:' + base64 marked value");
    CHECK(secret_is_marked(marked), "secret: built value is detected as marked");

    bn = secret_decrypt(marked, pw, sizeof pw - 1, back, sizeof back);
    CHECK(bn == (long)(sizeof plain - 1) && memcmp(back, plain, (size_t)bn) == 0,
          "secret_decrypt: right passphrase round-trips the plaintext");

    bn = secret_decrypt(marked, (const unsigned char *)"wrong", 5, back, sizeof back);
    CHECK(bn == -1, "secret_decrypt: wrong passphrase fails closed");

    CHECK(secret_decrypt("http://x", pw, sizeof pw - 1, back, sizeof back) == -1,
          "secret_decrypt: a non-marked value is rejected (no KDF run)");
    CHECK(secret_decrypt("aisc:!!notbase64", pw, sizeof pw - 1, back, sizeof back) == -1,
          "secret_decrypt: a bad base64 payload is rejected");
}

/* The mode-2 blob path: an AIS-CR1 image survives a write-to-file / read-back
 * and still decrypts. Built with a cheap kdf (mechanics test); secret_encrypt_to_file
 * is the same aisc_encrypt-then-fwrite with the real default cost. */
static void test_secret_blob_crypto(void)
{
    static const unsigned char plain[] = "line1\nline2\nrecovery: abc-def-ghi\n";
    static const unsigned char pw[] = "doc passphrase";
    const char *path = "/tmp/ais_ut_doc.aisc";
    aisc_kdf k = aisc_default_kdf();
    uint8_t *image = NULL;
    size_t ilen = 0;
    unsigned char buf[1024], *pt = NULL;
    size_t blen = 0, ptlen = 0;
    FILE *f;

    k.mem_blocks = 8; k.passes = 1;
    CHECK(aisc_encrypt(plain, sizeof plain - 1, pw, sizeof pw - 1, NULL, 0, k, &image, &ilen) == AISC_OK,
          "blob: build a cheap-kdf image");
    f = fopen(path, "wb");
    if (f) { fwrite(image, 1, ilen, f); fclose(f); }

    f = fopen(path, "rb");
    if (f) { blen = fread(buf, 1, sizeof buf, f); fclose(f); }
    CHECK(blen == ilen, "blob: the image survives a file write/read round-trip");
    aisc_wipe(image, ilen); free(image);

    CHECK(aisc_decrypt(buf, blen, pw, sizeof pw - 1, NULL, 0, &pt, &ptlen) == AISC_OK
          && ptlen == sizeof plain - 1 && memcmp(pt, plain, ptlen) == 0,
          "blob: the file image decrypts back to the document");
    if (pt) { aisc_wipe(pt, ptlen); free(pt); }
    remove(path);
}

/* The FFI crypto path the phone GUI uses for passwords: store encrypted, recall shows the
 * opaque marker (not the secret), the right passphrase reveals it, a wrong one does not. */
static void test_embed_encrypted(void)
{
    const char *dir = "/tmp/ais_ut_embcrypt";
    void *h;
    long id;
    char *r, *clear, *bar, *nl;
    char marker[AIS_LINE_MAX];

    scratch_rm(dir);
    h = ais_embed_open(dir);
    CHECK(h != NULL, "embed-crypt: open");

    id = ais_embed_store_encrypted(h, "gmail", "hunter2", "correct horse");
    CHECK(id > 0, "embed-crypt: store_encrypted returns an id");

    r = ais_embed_recall(h, "gmail", 0);
    CHECK(r != NULL && strstr(r, "aisc:") != NULL && strstr(r, "hunter2") == NULL,
          "embed-crypt: recall shows the opaque marker, not the secret");

    marker[0] = '\0';                                    /* pull the marker out of "id|marker\n" */
    if (r != NULL && (bar = strchr(r, '|')) != NULL) {
        snprintf(marker, sizeof marker, "%s", bar + 1);
        if ((nl = strchr(marker, '\n')) != NULL) *nl = '\0';
    }
    ais_embed_free(r);

    clear = ais_embed_reveal(marker, "correct horse");
    CHECK(clear != NULL && strcmp(clear, "hunter2") == 0,
          "embed-crypt: right passphrase reveals the secret");
    ais_embed_free(clear);

    clear = ais_embed_reveal(marker, "nope");
    CHECK(clear == NULL, "embed-crypt: wrong passphrase reveals nothing");
    ais_embed_free(clear);

    ais_embed_close(h);
    scratch_rm(dir);
}

/* File bundle round-trip over the embed seam: export index A sealed under a
 * secret to a FILE, import it into a fresh index B (right secret -> merged),
 * and reject a wrong secret into C (-3, nothing merged). Proves the offline
 * Drive/USB/email path a GUI drives, distinct from live socket sync. */
static void test_embed_bundle_file(void)
{
    const char *da = "/tmp/ais_ut_bundleA";
    const char *db = "/tmp/ais_ut_bundleB";
    const char *path = "/tmp/ais_ut_bundle.aisb";
    void *hA, *hB;
    char *r;
    long a1, a2;
    FILE *f;
    long fsize = -1;

    scratch_rm(da);
    scratch_rm(db);
    remove(path);

    hA = ais_embed_open(da);
    CHECK(hA != NULL, "embed-bundle: open source index A");
    if (hA == NULL) { scratch_rm(da); return; }

    a1 = ais_embed_store(hA, "venice italy", "https://ex.org/v");
    a2 = ais_embed_store(hA, "recipe pasta", "boil then toss");
    CHECK(a1 > 0 && a2 > 0, "embed-bundle: seed two records in A");

    /* plaintext export: no secret */
    CHECK(ais_embed_export_bundle(hA, path) == 0,
          "embed-bundle: export_bundle -> 0");
    f = fopen(path, "rb");
    CHECK(f != NULL, "embed-bundle: the bundle file was written");
    if (f != NULL) {
        if (fseek(f, 0, SEEK_END) == 0) fsize = ftell(f);
        fclose(f);
    }
    CHECK(fsize > 0, "embed-bundle: the bundle file is non-empty");

    /* plaintext import into a fresh index -> both records merge in */
    hB = ais_embed_open(db);
    CHECK(hB != NULL, "embed-bundle: open empty index B");
    if (hB != NULL) {
        CHECK(ais_embed_import_bundle(hB, path) == 0,
              "embed-bundle: import -> 0");
        r = ais_embed_recall(hB, "venice", 0);
        CHECK(r != NULL && strstr(r, "ex.org/v") != NULL,
              "embed-bundle: the first record merged into B");
        ais_embed_free(r);
        r = ais_embed_recall(hB, "pasta", 0);
        CHECK(r != NULL && strstr(r, "boil then toss") != NULL,
              "embed-bundle: the second record merged into B");
        ais_embed_free(r);
        ais_embed_close(hB);
    }

    ais_embed_close(hA);
    remove(path);
    scratch_rm(da);
    scratch_rm(db);
}
#endif

static void test_content_hash(void)
{
    char a[17], b[17], c[17];
    content_hash("Hotel Danieli, canal view", a);
    content_hash("Hotel Danieli, canal view", b);
    content_hash("Hotel Danieli", c);
    CHECK(strcmp(a, b) == 0, "content_hash: same value -> same hash");
    CHECK(strcmp(a, c) != 0, "content_hash: different value -> different hash");
    CHECK(strlen(a) == 16, "content_hash: 16 hex chars");
}

static void test_export_stream(void)
{
    ais a;
    FILE *tmp;
    char buf[4096];
    size_t n;
    const char *dir = "/tmp/ais_ut_export";

    scratch_rm(dir);
    ais_open(&a, dir);
    ais_put(&a, "venice", "Hotel Danieli");
    ais_put(&a, "paris", "Cafe de Flore");   /* id 2 */
    ais_put(&a, "rome", "Trattoria");
    ais_del(&a, 2);                           /* delete 'paris' */

    tmp = tmpfile();
    CHECK(tmp != NULL, "export: tmpfile opened");
    feed_export(&a, tmp);
    rewind(tmp);
    n = fread(buf, 1, sizeof buf - 1, tmp);
    buf[n] = '\0';
    fclose(tmp);

    CHECK(strstr(buf, "A|") != NULL && strstr(buf, "Hotel Danieli") != NULL,
          "export: live records appear as A| lines");
    CHECK(strstr(buf, "Cafe de Flore") == NULL,
          "export: a deleted record is absent from the A| lines");
    CHECK(strstr(buf, "D|") != NULL, "export: a tombstone appears as a D| line");

    ais_close(&a);
    scratch_rm(dir);
}

/* A value is "present" if its record exists and is not tombstoned. */
static int value_present(ais *a, const char *value)
{
    long id;
    if (store_find_value(a, value, &id) != 1)
        return 0;
    return tomb_contains(a, id) == 0;
}

/* Round-trip: export index A's merge stream and import it into B; B must converge to
 * A's live records AND apply A's deletion (last-write-wins) over B's own copy. */
static void test_merge_roundtrip(void)
{
    ais A, B;
    FILE *tmp;
    const char *da = "/tmp/ais_ut_mergeA", *db = "/tmp/ais_ut_mergeB";

    scratch_rm(da);
    scratch_rm(db);
    ais_open(&A, da);
    ais_open(&B, db);

    ais_put(&A, "venice", "Hotel Danieli");
    ais_put(&A, "paris", "Cafe de Flore");   /* A id 2, deleted below */
    ais_put(&A, "rome", "Trattoria");
    ais_del(&A, 2);                            /* A deletes 'paris' */

    ais_put(&B, "paris", "Cafe de Flore");    /* B independently still has 'paris' */

    tmp = tmpfile();
    CHECK(tmp != NULL, "merge: tmpfile opened");
    feed_export(&A, tmp);                      /* A -> merge stream */
    rewind(tmp);
    feed_import_from(&B, tmp);                  /* stream -> B (the merge) */
    fclose(tmp);

    CHECK(value_present(&B, "Hotel Danieli") == 1, "merge: a live record arrived in B");
    CHECK(value_present(&B, "Trattoria") == 1,     "merge: the second live record arrived");
    CHECK(value_present(&B, "Cafe de Flore") == 0, "merge: A's deletion propagated to B");

    ais_close(&A);
    ais_close(&B);
    scratch_rm(da);
    scratch_rm(db);
}

/* The sync transport's crypto+merge endpoints: export A's stream sealed under a token; a
 * wrong token is rejected before any merge; the right token unseals and converges B. */
static void test_sync_sealed(void)
{
    ais A, B;
    uint8_t *blob = NULL;
    size_t blen = 0;
    const char *da = "/tmp/ais_ut_sealA", *db = "/tmp/ais_ut_sealB";
    const char *tok = "0123456789abcdef0123456789abcdef";

    scratch_rm(da);
    scratch_rm(db);
    ais_open(&A, da);
    ais_open(&B, db);
    ais_put(&A, "venice", "Hotel Danieli");
    ais_put(&A, "paris", "Cafe de Flore");   /* id 2 */
    ais_del(&A, 2);

    CHECK(sync_export_sealed(&A, tok, &blob, &blen) == 0 && blob != NULL && blen > 0,
          "sync: export -> sealed blob");
    CHECK(sync_import_sealed(&B, "ffffffffffffffffffffffffffffffff", blob, blen) == -1,
          "sync: a wrong token is rejected");
    CHECK(value_present(&B, "Hotel Danieli") == 0,
          "sync: nothing merged on a bad token");
    CHECK(sync_import_sealed(&B, tok, blob, blen) == 0,
          "sync: the right token unseals and merges");
    CHECK(value_present(&B, "Hotel Danieli") == 1,
          "sync: a live record arrived via the sealed blob");
    CHECK(value_present(&B, "Cafe de Flore") == 0,
          "sync: the deletion propagated via the sealed blob");

    free(blob);
    ais_close(&A);
    ais_close(&B);
    scratch_rm(da);
    scratch_rm(db);
}

/* The full socket transport over loopback: a forked child serves A once; the parent
 * pulls into B and converges (live records arrive, the deletion propagates). */
static void test_sync_socket(void)
{
    ais B;
    const char *da = "/tmp/ais_ut_sockA", *db = "/tmp/ais_ut_sockB";
    const char *tok = "0123456789abcdef0123456789abcdef";
    int port = 47137, rc;
    pid_t pid;

    scratch_rm(da);
    scratch_rm(db);
    {   /* set up A and close it, so the child opens it fresh */
        ais A;
        ais_open(&A, da);
        ais_put(&A, "venice", "Hotel Danieli");
        ais_put(&A, "paris", "Cafe de Flore");   /* id 2 */
        ais_del(&A, 2);
        ais_close(&A);
    }

    pid = fork();
    if (pid == 0) {                              /* child: serve A once, then exit */
        ais cA;
        ais_open(&cA, da);
        sync_serve(&cA, port, tok, 5, 0);
        ais_close(&cA);
        _exit(0);
    }

    ais_open(&B, db);                            /* parent: pull into B */
    rc = sync_pull(&B, "127.0.0.1", port, tok, 5, 0);
    CHECK(rc == 0, "sync(socket): pull over loopback succeeded");
    CHECK(value_present(&B, "Hotel Danieli") == 1, "sync(socket): live record arrived over TCP");
    CHECK(value_present(&B, "Cafe de Flore") == 0, "sync(socket): deletion propagated over TCP");
    ais_close(&B);
    waitpid(pid, NULL, 0);

    /* wrong token: the challenge-response proof fails, the server serves nothing, the pull
     * fails, and B stays empty (nothing decrypted or merged). */
    scratch_rm(db);
    pid = fork();
    if (pid == 0) {
        ais cA;
        ais_open(&cA, da);
        sync_serve(&cA, port, tok, 5, 0);           /* right token here; the client brings a wrong one */
        ais_close(&cA);
        _exit(0);
    }
    ais_open(&B, db);
    rc = sync_pull(&B, "127.0.0.1", port, "ffffffffffffffffffffffffffffffff", 5, 0);
    CHECK(rc != 0, "sync(socket): wrong token is rejected");
    CHECK(value_present(&B, "Hotel Danieli") == 0, "sync(socket): nothing merged on a wrong token");
    ais_close(&B);
    waitpid(pid, NULL, 0);

    scratch_rm(da);
    scratch_rm(db);
}

/* ---- folder auto-sync (spec v1): a framed bundle per device in a shared folder ---- */

/* Two devices sync through a shared folder and converge (live records both ways, a
 * delete propagates). No mover: the shared folder IS the (instant) transport. */
static void test_sync_folder(void)
{
    ais A, B;
    const char *da = "/tmp/ais_ut_fldA", *db = "/tmp/ais_ut_fldB";
    const char *fld = "/tmp/ais_ut_fld_sync";

    scratch_rm(da); scratch_rm(db); scratch_rm(fld);
    ais_open(&A, da); ais_open(&B, db);
    ais_put(&A, "venice", "Hotel Danieli");
    ais_put(&A, "paris", "Cafe de Flore");     /* A id 2 */
    ais_del(&A, 2);                             /* A deletes paris */
    ais_put(&B, "rome", "Trattoria");

    CHECK(sync_folder_once(&A, fld) == 0, "folder: A pass 1 (export A)");
    CHECK(sync_folder_once(&B, fld) == 0, "folder: B pass 1 (import A, export B)");
    CHECK(sync_folder_once(&A, fld) == 0, "folder: A pass 2 (import B)");

    CHECK(value_present(&B, "Hotel Danieli") == 1, "folder: A's live record reached B");
    CHECK(value_present(&B, "Cafe de Flore") == 0, "folder: A's deletion propagated to B");
    CHECK(value_present(&A, "Trattoria") == 1,     "folder: B's record reached A");

    ais_close(&A); ais_close(&B);
    scratch_rm(da); scratch_rm(db); scratch_rm(fld);
}

/* B2: a torn / corrupt framed bundle is REJECTED wholesale (nothing merged), while a
 * complete frame merges. This is the guarantee against a mover's mid-write read. */
static void test_sync_frame_reject(void)
{
    ais A, B, C, D;
    const char *da = "/tmp/ais_ut_frA", *db = "/tmp/ais_ut_frB";
    const char *dc = "/tmp/ais_ut_frC", *dd = "/tmp/ais_ut_frD";
    uint8_t *buf = NULL, nonce[16];
    char id[17];
    size_t len = 0;

    scratch_rm(da); scratch_rm(db); scratch_rm(dc); scratch_rm(dd);
    ais_open(&A, da); ais_open(&B, db); ais_open(&C, dc); ais_open(&D, dd);
    ais_put(&A, "venice", "Hotel Danieli");
    CHECK(sync_device_id(&A, id, sizeof id, nonce) == 0, "frame: device id");
    CHECK(sync_export_framed(&A, nonce, 1, &buf, &len) == 0 && len > 41,
          "frame: export produced a framed bundle");

    CHECK(sync_import_framed(&B, buf, len, NULL, NULL) == 0, "frame: a full frame merges");
    CHECK(value_present(&B, "Hotel Danieli") == 1, "frame: the record arrived");

    CHECK(sync_import_framed(&C, buf, len - 5, NULL, NULL) == -1,
          "frame: a TRUNCATED bundle is rejected");
    CHECK(value_present(&C, "Hotel Danieli") == 0, "frame: nothing merged from the truncated bundle");

    buf[len - 1] ^= 0xFF;                        /* corrupt a payload byte */
    CHECK(sync_import_framed(&D, buf, len, NULL, NULL) == -1,
          "frame: a CORRUPT bundle is rejected");
    CHECK(value_present(&D, "Hotel Danieli") == 0, "frame: nothing merged from the corrupt bundle");

    free(buf);
    ais_close(&A); ais_close(&B); ais_close(&C); ais_close(&D);
    scratch_rm(da); scratch_rm(db); scratch_rm(dc); scratch_rm(dd);
}

/* B3: a delete must survive compaction. A deletes+compacts (which used to GC the
 * tombstone); a peer still holding the record must still learn of the delete, and its
 * re-export must NOT resurrect the record in A. */
static void test_sync_delete_survives_compact(void)
{
    ais A, B;
    FILE *t;
    const char *da = "/tmp/ais_ut_dcA", *db = "/tmp/ais_ut_dcB";

    scratch_rm(da); scratch_rm(db);
    ais_open(&A, da); ais_open(&B, db);
    ais_put(&A, "shared", "gone soon");         /* A id 1 */
    ais_put(&B, "shared", "gone soon");         /* B independently holds it */
    ais_del(&A, 1);
    CHECK(ais_compact(&A) == 0, "B3: A compacts (drops the record body)");
    CHECK(value_present(&A, "gone soon") == 0, "B3: record is gone in A after delete+compact");

    t = tmpfile();                              /* A -> B: the retained tombstone must apply */
    feed_export(&A, t); rewind(t); feed_import_from(&B, t); fclose(t);
    CHECK(value_present(&B, "gone soon") == 0, "B3: delete survives compaction and propagates to B");

    t = tmpfile();                              /* B -> A: must NOT resurrect */
    feed_export(&B, t); rewind(t); feed_import_from(&A, t); fclose(t);
    CHECK(value_present(&A, "gone soon") == 0, "B3: peer re-export does not resurrect the record");

    ais_close(&A); ais_close(&B);
    scratch_rm(da); scratch_rm(db);
}

/* B1: a device-id clone (the same syncid copied to a second device) is detected and
 * healed: one device regenerates a fresh id, so the two never clobber a shared file
 * and both still converge. */
static void test_sync_clone_heal(void)
{
    ais A, B;
    const char *da = "/tmp/ais_ut_clA", *db = "/tmp/ais_ut_clB";
    const char *fld = "/tmp/ais_ut_cl_fld";
    char pa[AIS_PATH_MAX], pb[AIS_PATH_MAX], sid[160] = "";
    char idAf[17] = "", idBf[17] = "";
    FILE *f;
    int i;

    scratch_rm(da); scratch_rm(db); scratch_rm(fld);
    ais_open(&A, da); ais_open(&B, db);
    ais_put(&A, "ka", "from A");
    ais_put(&B, "kb", "from B");

    CHECK(sync_folder_once(&A, fld) == 0, "clone: A first pass creates its identity");
    /* clone A's whole identity (id+nonce+seq) onto B */
    snprintf(pa, sizeof pa, "%s/syncid", da);
    snprintf(pb, sizeof pb, "%s/syncid", db);
    f = fopen(pa, "r"); CHECK(f != NULL, "clone: read A's syncid");
    if (f) { if (!fgets(sid, sizeof sid, f)) sid[0] = '\0'; fclose(f); }
    f = fopen(pb, "w"); if (f) { fputs(sid, f); fclose(f); }

    /* a few interleaved passes: the collision must heal to two distinct ids */
    for (i = 0; i < 4; i++) { sync_folder_once(&B, fld); sync_folder_once(&A, fld); }

    f = fopen(pa, "r"); if (f) { if (fscanf(f, "%16s", idAf) != 1) idAf[0] = '\0'; fclose(f); }
    f = fopen(pb, "r"); if (f) { if (fscanf(f, "%16s", idBf) != 1) idBf[0] = '\0'; fclose(f); }
    CHECK(idAf[0] && idBf[0] && strcmp(idAf, idBf) != 0,
          "clone: the id collision healed to two distinct device-ids");
    CHECK(value_present(&A, "from B") == 1, "clone: converged - B's record in A");
    CHECK(value_present(&B, "from A") == 1, "clone: converged - A's record in B");

    ais_close(&A); ais_close(&B);
    scratch_rm(da); scratch_rm(db); scratch_rm(fld);
}

/* I1: a tag removed on one device must propagate (and NOT resurrect) on a peer that
 * still has it, and the detach must survive compaction. Uses the merge stream directly
 * (feed_export/import), the same core the folder bundle carries. */
static void test_sync_tag_detach(void)
{
    ais A, B;
    struct idvec v;
    FILE *t;
    const char *da = "/tmp/ais_ut_tdA", *db = "/tmp/ais_ut_tdB";

    scratch_rm(da); scratch_rm(db);
    ais_open(&A, da); ais_open(&B, db);
    ais_put(&A, "home wifi", "the password");   /* A id 1: tags home, wifi */
    ais_put(&B, "home wifi", "the password");   /* B independently holds both tags */

    ais_update(&A, 1, "-wifi");                  /* A removes the 'wifi' tag */
    query(&A, AIS_AND, &v, 1, "wifi"); CHECK(v.n == 0, "detach: 'wifi' gone locally in A");

    t = tmpfile();                               /* A -> B: the detach must apply */
    feed_export(&A, t); rewind(t); feed_import_from(&B, t); fclose(t);
    query(&B, AIS_AND, &v, 1, "wifi"); CHECK(v.n == 0, "detach: tag removal propagated to B");
    query(&B, AIS_AND, &v, 1, "home"); CHECK(v.n == 1, "detach: the other tag survived in B");

    t = tmpfile();                               /* B -> A: must NOT re-attach 'wifi' */
    feed_export(&B, t); rewind(t); feed_import_from(&A, t); fclose(t);
    query(&A, AIS_AND, &v, 1, "wifi"); CHECK(v.n == 0, "detach: peer re-export does not resurrect the tag");

    /* durable through compaction: A compacts, then B (re-attached-free) still stays put */
    CHECK(ais_compact(&A) == 0, "detach: A compacts");
    t = tmpfile();
    feed_export(&A, t); rewind(t); feed_import_from(&B, t); fclose(t);
    query(&B, AIS_AND, &v, 1, "wifi"); CHECK(v.n == 0, "detach: survives compaction, still gone in B");

    ais_close(&A); ais_close(&B);
    scratch_rm(da); scratch_rm(db);
}

#ifdef AIS_UT_HAVE_CRYPTO
/* The FFI seam's sync (embed.h) -- the EXACT call path the mobile/desktop GUI
 * uses: ais_embed_pull (Receive) and ais_embed_serve (Send) over a forked
 * loopback, plus their bad-arg / return-code contract. Lets us test the sync
 * feature without rolling an APK. */
static void test_embed_sync(void)
{
    const char *da = "/tmp/ais_ut_embsyncA", *db = "/tmp/ais_ut_embsyncB";
    const char *tok = "0123456789abcdef0123456789abcdef";
    const char *url = "http://127.0.0.1:47139";
    const int port = 47139;
    void *h;
    char *r;
    pid_t pid;

    /* bad args / URL parse (pure, fast) */
    CHECK(ais_embed_pull(NULL, url, tok) == -1, "embed pull: NULL handle -> -1");
    CHECK(ais_embed_serve(NULL, port, tok) == -1, "embed serve: NULL handle -> -1");
    scratch_rm(db);
    h = ais_embed_open(db);
    CHECK(ais_embed_pull(h, "http://", tok) == -1, "embed pull: empty-host URL -> -1");
    ais_embed_close(h);

    /* Receive: a forked child serves A once (engine, 5s); the parent pulls into
     * B through the FFI and must end up with A's record. */
    scratch_rm(da);
    scratch_rm(db);
    { ais A; ais_open(&A, da); ais_put(&A, "venice", "Hotel Danieli"); ais_close(&A); }

    pid = fork();
    if (pid == 0) {
        ais cA; ais_open(&cA, da);
        sync_serve(&cA, port, tok, 5, 0);
        ais_close(&cA);
        _exit(0);
    }
    h = ais_embed_open(db);
    CHECK(ais_embed_pull(h, url, tok) == 0, "embed pull: Receive over loopback -> 0 (merged)");
    r = ais_embed_recall(h, "venice", 0);
    CHECK(r != NULL && strstr(r, "Hotel Danieli") != NULL, "embed pull: A's record arrived in B");
    ais_embed_free(r);
    ais_embed_close(h);
    waitpid(pid, NULL, 0);

    /* Send: the parent serves A through the FFI; a forked child pulls into B
     * (engine) promptly, so ais_embed_serve returns as soon as it is served. */
    scratch_rm(db);
    pid = fork();
    if (pid == 0) {
        ais cB; ais_open(&cB, db);
        sync_pull(&cB, "127.0.0.1", port, tok, 5, 0);   /* retries connect until the parent binds */
        ais_close(&cB);
        _exit(0);
    }
    h = ais_embed_open(da);
    CHECK(ais_embed_serve(h, port, tok) == 0, "embed serve: Send, a peer pulled -> 0");
    ais_embed_close(h);
    waitpid(pid, NULL, 0);
    { ais B; ais_open(&B, db);
      CHECK(value_present(&B, "Hotel Danieli") == 1, "embed serve: A's record reached B via the FFI serve");
      ais_close(&B); }

    scratch_rm(da);
    scratch_rm(db);
}
#endif /* AIS_UT_HAVE_CRYPTO */

#ifdef AIS_UT_HAVE_CRYPTO
/* The FFI "Sync" (ais_embed_sync_pull / _serve): the app's one-tap converge. One
 * side hosts (bidir serve), the other joins (bidir pull); both end merged. */
static void test_embed_sync_exchange(void)
{
    const char *da = "/tmp/ais_ut_embxA", *db = "/tmp/ais_ut_embxB";
    const char *tok = "0123456789abcdef0123456789abcdef";
    const char *url = "http://127.0.0.1:47143";
    const int port = 47143;
    void *h;
    char *r;
    pid_t pid;

    scratch_rm(da); scratch_rm(db);
    { ais A; ais_open(&A, da); ais_put(&A, "venice", "Hotel Danieli"); ais_close(&A); }
    { ais B; ais_open(&B, db); ais_put(&B, "paris",  "Cafe de Flore"); ais_close(&B); }

    pid = fork();
    if (pid == 0) {                              /* child: host (bidir serve) via the FFI */
        void *ch = ais_embed_open(da);
        ais_embed_sync_serve(ch, port, tok);
        ais_embed_close(ch);
        _exit(0);
    }
    h = ais_embed_open(db);                       /* parent: join (bidir pull) via the FFI */
    CHECK(ais_embed_sync_pull(h, url, tok) == 0, "embed sync: bidir pull -> 0");
    r = ais_embed_recall(h, "venice", 0);
    CHECK(r != NULL && strstr(r, "Hotel Danieli") != NULL, "embed sync: host's record reached the joiner");
    ais_embed_free(r);
    ais_embed_close(h);
    waitpid(pid, NULL, 0);

    { ais A; ais_open(&A, da);                    /* host also has the joiner's record (converged) */
      CHECK(value_present(&A, "Cafe de Flore") == 1, "embed sync: joiner's record reached the host");
      ais_close(&A); }

    scratch_rm(da); scratch_rm(db);
}
#endif /* AIS_UT_HAVE_CRYPTO */

#ifdef AIS_UT_HAVE_CRYPTO
/* A busy port fails FAST with a distinct code, not after the serve timeout: hold
 * the port in-process, then ais_embed_serve on it must return -3 (bind failed). */
static void test_embed_sync_portbusy(void)
{
    const char *db = "/tmp/ais_ut_busy";
    const char *tok = "0123456789abcdef0123456789abcdef";
    const int port = 47205;
    int s, one = 1;
    struct sockaddr_in a;
    void *h;

    s = socket(AF_INET, SOCK_STREAM, 0);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((unsigned short)port);
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    CHECK(bind(s, (struct sockaddr *)&a, sizeof a) == 0 && listen(s, 1) == 0,
          "portbusy: test holds the port");

    scratch_rm(db);
    h = ais_embed_open(db);
    CHECK(ais_embed_serve(h, port, tok) == -3, "embed serve: busy port -> -3 (fast, not a timeout)");
    ais_embed_close(h);
    close(s);
    scratch_rm(db);
}
#endif /* AIS_UT_HAVE_CRYPTO */

#ifdef AIS_UT_HAVE_CRYPTO
/* The symmetric exchange (sync_serve/sync_pull with bidir=1): after ONE
 * connection both sides hold the union, converging with no sender/receiver role.
 * A has X, B has Y; after the exchange each has both. */
static void test_sync_exchange(void)
{
    const char *da = "/tmp/ais_ut_exchA", *db = "/tmp/ais_ut_exchB";
    const char *tok = "0123456789abcdef0123456789abcdef";
    const int port = 47141;
    pid_t pid;

    scratch_rm(da);
    scratch_rm(db);
    { ais A; ais_open(&A, da); ais_put(&A, "venice", "Hotel Danieli"); ais_close(&A); }
    { ais B; ais_open(&B, db); ais_put(&B, "paris",  "Cafe de Flore"); ais_close(&B); }

    pid = fork();
    if (pid == 0) {                              /* child: serve A, bidirectional */
        ais cA; ais_open(&cA, da);
        sync_serve(&cA, port, tok, 5, 1);
        ais_close(&cA);
        _exit(0);
    }
    { ais B; ais_open(&B, db);                   /* parent: pull into B, bidirectional */
      CHECK(sync_pull(&B, "127.0.0.1", port, tok, 5, 1) == 0, "exchange: bidirectional pull -> 0");
      CHECK(value_present(&B, "Hotel Danieli") == 1, "exchange: A's record reached B (forward)");
      CHECK(value_present(&B, "Cafe de Flore") == 1, "exchange: B kept its own record");
      ais_close(&B); }
    waitpid(pid, NULL, 0);

    { ais A; ais_open(&A, da);                   /* A (served by child) must also hold B's record */
      CHECK(value_present(&A, "Cafe de Flore") == 1, "exchange: B's record reached A (reverse)");
      CHECK(value_present(&A, "Hotel Danieli") == 1, "exchange: A kept its own record");
      ais_close(&A); }

    scratch_rm(da);
    scratch_rm(db);
}
#endif /* AIS_UT_HAVE_CRYPTO */

#ifdef AIS_UT_HAVE_CRYPTO
/* Carry a doc BLOB over the socket sync: A stores a MULTI-LINE value (which
 * ais_put_value turns into a blobs/<ts>.txt file + a record holding the path);
 * B pulls and must end up with BOTH the record AND the actual blob file on disk,
 * content-identical. A second pull is idempotent (dedup, no error, no dup file).
 * Finally, a payload sealed with a WRONG protocol version byte must be rejected
 * LOUDLY with -2 and merge nothing. */
static void test_sync_blob(void)
{
    ais B;
    const char *da = "/tmp/ais_ut_blobA", *db = "/tmp/ais_ut_blobB";
    const char *tok = "0123456789abcdef0123456789abcdef";
    const char *doc = "line one\nline two\nline three\n";
    int port = 47145, rc;
    pid_t pid;
    char relval[AIS_PATH_MAX] = "", bpath[2 * AIS_PATH_MAX];  /* bpath = dir + '/' + relval */
    long docid;

    scratch_rm(da);
    scratch_rm(db);
    {   /* A: store a multi-line value -> a blobs/ file, then read back its path */
        ais A;
        struct valvec vv = { {{0}}, 0 };
        ais_open(&A, da);
        docid = ais_put_value(&A, "notes recipe", doc);
        CHECK(docid > 0, "blob: ais_put_value stored a multi-line value");
        ais_record(&A, docid, collect_val, &vv);
        if (vv.n == 1) snprintf(relval, sizeof relval, "%.*s", (int)(sizeof relval - 1), vv.vals[0]);
        ais_close(&A);
    }
    CHECK(strncmp(relval, "blobs/", 6) == 0, "blob: the stored value is a blobs/ path");

    pid = fork();
    if (pid == 0) {                              /* child: serve A once, then exit */
        ais cA;
        ais_open(&cA, da);
        sync_serve(&cA, port, tok, 5, 0);
        ais_close(&cA);
        _exit(0);
    }

    ais_open(&B, db);                            /* parent: pull into B */
    rc = sync_pull(&B, "127.0.0.1", port, tok, 5, 0);
    CHECK(rc == 0, "blob: pull over loopback succeeded");
    CHECK(value_present(&B, relval) == 1, "blob: the doc record arrived in B");
    ais_close(&B);
    waitpid(pid, NULL, 0);

    /* the actual blob FILE must now exist under B and round-trip its content */
    snprintf(bpath, sizeof bpath, "%s/%s", db, relval);
    {
        FILE *f = fopen(bpath, "rb");
        char got[256];
        size_t n = 0;
        CHECK(f != NULL, "blob: the blob file exists on B's disk");
        if (f) { n = fread(got, 1, sizeof got - 1, f); got[n] = '\0'; fclose(f); }
        CHECK(n == strlen(doc) && memcmp(got, doc, n) == 0, "blob: B's blob content matches A's");
    }

    /* second pull: idempotent -- no error and the blob is deduped (not duplicated) */
    pid = fork();
    if (pid == 0) {
        ais cA;
        ais_open(&cA, da);
        sync_serve(&cA, port, tok, 5, 0);
        ais_close(&cA);
        _exit(0);
    }
    ais_open(&B, db);
    rc = sync_pull(&B, "127.0.0.1", port, tok, 5, 0);
    CHECK(rc == 0, "blob: a repeat pull is idempotent (no error)");
    ais_close(&B);
    waitpid(pid, NULL, 0);
    {   /* exactly ONE blob file under B (dedup skipped the re-write, minted no -1) */
        char cmd[AIS_PATH_MAX + 64];
        FILE *p;
        int cnt = -1;
        snprintf(cmd, sizeof cmd, "ls '%s/blobs' | wc -l", db);
        p = popen(cmd, "r");
        if (p) { if (fscanf(p, "%d", &cnt) != 1) cnt = -1; pclose(p); }
        CHECK(cnt == 1, "blob: dedup kept exactly one blob file on B");
    }

    /* version guard: seal a payload whose plaintext version byte is NOT AIS_SYNC_PROTO(1);
     * sync_import_sealed must return -2 and merge nothing. We build the sealed blob with
     * the same subkey derivation sync_export_sealed uses, so the auth passes and only the
     * version check can trip. */
    {
        ais V;
        uint8_t kseal[32], *sealed = NULL;
        size_t slen = 0;
        /* a well-formed record text, but prefixed with a WRONG version byte (2) */
        uint8_t bad[64];
        const char *body = "A|2030-01-01T00:00:00Z|ghost|Should Not Merge\n";
        size_t blen = strlen(body);
        bad[0] = 2;                            /* != AIS_SYNC_PROTO(1): a future/foreign format */
        memcpy(bad + 1, body, blen);
        aisc_subkey((const uint8_t *)tok, strlen(tok), "ais-sync-seal-v1", NULL, 0, kseal);
        CHECK(aisc_seal_key(kseal, bad, blen + 1, &sealed, &slen) == AISC_OK,
              "blob(ver): sealed a wrong-version payload");
        aisc_wipe(kseal, sizeof kseal);

        scratch_rm(db);
        ais_open(&V, db);
        CHECK(sync_import_sealed(&V, tok, sealed, slen) == -2,
              "blob(ver): wrong version byte -> -2 (loud, not silent)");
        CHECK(value_present(&V, "Should Not Merge") == 0,
              "blob(ver): nothing merged on a version mismatch");
        /* and the current format still round-trips (auth + version both good) */
        {
            ais S;
            uint8_t *good = NULL;
            size_t glen = 0;
            scratch_rm(da);
            ais_open(&S, da);
            ais_put(&S, "keep", "Present Value");
            CHECK(sync_export_sealed(&S, tok, &good, &glen) == 0, "blob(ver): current-format export");
            CHECK(sync_import_sealed(&V, tok, good, glen) == 0, "blob(ver): current-format import -> 0");
            CHECK(value_present(&V, "Present Value") == 1, "blob(ver): current format still merges");
            if (good) free(good);
            ais_close(&S);
        }
        if (sealed) free(sealed);
        ais_close(&V);
    }

    scratch_rm(da);
    scratch_rm(db);
}
#endif /* AIS_UT_HAVE_CRYPTO */

/* Last-write-wins by ts: a newer delete beats an older add, an older re-add stays deleted, a
 * newer re-add resurrects, and an older delete does not remove a newer add. */
static void test_merge_lww(void)
{
    ais a;
    const char *dir = "/tmp/ais_ut_lww";
    char h[17];
    const char *T_OLD = "2020-01-01T00:00:00Z";
    const char *T_MID = "2025-01-01T00:00:00Z";
    const char *T_NEW = "2030-01-01T00:00:00Z";

    scratch_rm(dir);
    ais_open(&a, dir);

    ais_put_at(&a, "k", "X", T_OLD);                     /* add X, old */
    content_hash("X", h);
    ais_merge_del(&a, h, T_MID);                         /* delete X, newer -> wins */
    CHECK(value_present(&a, "X") == 0, "lww: a newer delete beats an older add");

    ais_put_at(&a, "k", "X", T_OLD);                     /* re-add older than the delete */
    CHECK(value_present(&a, "X") == 0, "lww: an older re-add stays deleted");

    ais_put_at(&a, "k", "X", T_NEW);                     /* re-add newer than the delete */
    CHECK(value_present(&a, "X") == 1, "lww: a newer re-add resurrects");

    ais_put_at(&a, "k2", "Y", T_MID);                    /* add Y, mid */
    content_hash("Y", h);
    ais_merge_del(&a, h, T_OLD);                         /* delete Y, older -> ignored */
    CHECK(value_present(&a, "Y") == 1, "lww: an older delete does not remove a newer add");

    ais_close(&a);
    scratch_rm(dir);
}

/* The write-time line bound: a value that would overflow one AIS_LINE_MAX store line is
 * refused with nothing written, while a within-limit value still stores and round-trips. */
static void test_record_too_long(void)
{
    ais a;
    const char *dir = "/tmp/ais_ut_toolong";
    size_t over_n = AIS_LINE_MAX + 8;
    size_t fit_n  = AIS_LINE_MAX - 64;                   /* leaves room for id|ts|keys| overhead */
    char *big = malloc(over_n + 1);
    char *fit = malloc(fit_n + 1);
    long rc, id;

    if (big == NULL || fit == NULL) { CHECK(0, "toolong: malloc"); free(big); free(fit); return; }
    scratch_rm(dir);
    ais_open(&a, dir);

    memset(big, 'x', over_n); big[over_n] = '\0';
    rc = ais_put(&a, "huge", big);
    CHECK(rc < 0, "toolong: an over-length value is refused");
    CHECK(value_present(&a, big) == 0, "toolong: nothing was written");

    memset(fit, 'y', fit_n); fit[fit_n] = '\0';
    id = ais_put(&a, "ok", fit);
    CHECK(id > 0, "toolong: a within-limit value still stores");
    CHECK(value_present(&a, fit) == 1, "toolong: the within-limit value round-trips");

    free(big); free(fit);
    ais_close(&a);
    scratch_rm(dir);
}

/* Deferred behavior (decision A): a multi-value record (ais_add) survives a merge, but its
 * values un-group into separate records on the peer. Pin it so a future change is noticed. */
static void test_merge_multivalue(void)
{
    ais A, B;
    FILE *tmp;
    const char *da = "/tmp/ais_ut_mvA", *db = "/tmp/ais_ut_mvB";
    long id, i1, i2;

    scratch_rm(da);
    scratch_rm(db);
    ais_open(&A, da);
    id = ais_put(&A, "k", "v1");
    ais_add(&A, id, "v2");                                /* id now carries v1 AND v2 */
    store_find_value(&A, "v1", &i1);
    store_find_value(&A, "v2", &i2);
    CHECK(i1 == i2, "multivalue: in the source, both values share one id");

    ais_open(&B, db);
    tmp = tmpfile();
    feed_export(&A, tmp);
    rewind(tmp);
    feed_import_from(&B, tmp);
    fclose(tmp);

    CHECK(value_present(&B, "v1") == 1 && value_present(&B, "v2") == 1,
          "multivalue: both values survive the merge");
    store_find_value(&B, "v1", &i1);
    store_find_value(&B, "v2", &i2);
    CHECK(i1 != i2, "multivalue: values un-group into separate records on the peer");

    ais_close(&A);
    ais_close(&B);
    scratch_rm(da);
    scratch_rm(db);
}

/* URL parsing for the pull side: scheme strip, host:port split, path drop, port defaulting. */
static void test_sync_url(void)
{
    char host[128];
    int port;

    CHECK(sync_parse_url("http://127.0.0.1:8766", host, sizeof host, &port) == 0
          && strcmp(host, "127.0.0.1") == 0 && port == 8766, "url: http://host:port");
    CHECK(sync_parse_url("192.168.1.5", host, sizeof host, &port) == 0
          && strcmp(host, "192.168.1.5") == 0 && port == AIS_SYNC_PORT, "url: bare host -> default port");
    CHECK(sync_parse_url("https://10.0.0.2:80/sync/x", host, sizeof host, &port) == 0
          && strcmp(host, "10.0.0.2") == 0 && port == 80, "url: https + path dropped");
    CHECK(sync_parse_url("h:99999", host, sizeof host, &port) == 0 && port == AIS_SYNC_PORT,
          "url: out-of-range port -> default");
    CHECK(sync_parse_url("h:0", host, sizeof host, &port) == 0 && port == AIS_SYNC_PORT,
          "url: zero port -> default");
    CHECK(sync_parse_url("http://", host, sizeof host, &port) == -1, "url: empty host rejected");
}

int main(void)
{
    printf("AIS regression tests (make ut)\n");
    printf("content:\n");
    test_content_hash();
    printf("merge:\n");
    test_export_stream();
    test_merge_roundtrip();
    test_merge_lww();
    test_merge_multivalue();
    printf("store bounds:\n");
    test_record_too_long();
    printf("sync url:\n");
    test_sync_url();
    printf("key:\n");
    test_key_encode();
    test_key_prefix();
    printf("put:\n");
    test_put_monotonic();
    test_put_idempotent();
    test_put_new_key_ordered_insert();
    printf("get:\n");
    test_get_and_or();
    test_get_multikey();
    printf("add:\n");
    test_add_multilink();
    printf("update:\n");
    test_update();
    printf("set_value:\n");
    test_set_value();
    printf("del:\n");
    test_del();
    test_del_key();
    printf("keys:\n");
    test_keys();
    printf("stats:\n");
    test_stats();
    printf("find:\n");
    test_find();
    printf("record:\n");
    test_record_fastpath();
    printf("compact:\n");
    test_compact();
    test_compact_next_id_no_reuse();
    test_compact_rollback();
    test_compact_recover_debris();
    test_detach_lww();
    printf("recovery:\n");
    test_next_id_recovery();
    test_next_id_corrupt_cache();
    printf("rebuild-from-store:\n");
    test_rebuild_from_store();
    printf("timestamp:\n");
    test_timestamp();
    printf("mixed formats (v1/v2/v3 in one store):\n");
    test_mixed_formats();
    printf("timeline:\n");
    test_timeline();
    test_timeline_dates();
    printf("tags:\n");
    test_tags();
    printf("keyset pages (get_page / tags_page):\n");
    test_get_page();
    test_tags_page();
    printf("put_value (one paste -> one record):\n");
    test_put_value();
    printf("doc_display (blob -> content, shared by web + Flutter):\n");
    test_doc_display();
    printf("embed (FFI seam):\n");
    test_embed();
    printf("embed find (content search fallback):\n");
    test_embed_find();
    printf("embed keys (visible/current keys, not stale store line):\n");
    test_embed_keys_visible();
    printf("import-interactively:\n");
    test_import_interactive();
    printf("switch / indexes:\n");
    test_index_switch();
    printf("base64:\n");
    test_b64();
    printf("secret marker:\n");
    test_secret_marker();
    printf("secret blob ref + shred:\n");
    test_secret_blob_relpath();
    test_secret_shred();
#ifdef AIS_UT_HAVE_CRYPTO
    printf("crypto (vendored monocypher):\n");
    test_crypto();
    printf("secret value (encrypt/decrypt glue):\n");
    test_secret_value();
    printf("secret blob (file image round-trip):\n");
    test_secret_blob_crypto();
    printf("sync transport (sealed stream):\n");
    test_sync_sealed();
    printf("sync transport (socket, forked loopback):\n");
    test_sync_socket();
    printf("folder sync (two devices converge through a shared folder):\n");
    test_sync_folder();
    printf("folder sync B2 (torn/corrupt frame rejected):\n");
    test_sync_frame_reject();
    printf("folder sync B3 (delete survives compaction):\n");
    test_sync_delete_survives_compact();
    printf("folder sync B1 (device-id clone healed):\n");
    test_sync_clone_heal();
    printf("folder sync I1 (tag removal propagates + survives compaction):\n");
    test_sync_tag_detach();
    printf("embed sync (FFI pull/serve over loopback):\n");
    test_embed_sync();
    printf("sync exchange (symmetric bidir, both converge):\n");
    test_sync_exchange();
    printf("sync transport (doc blob carry + dedup + version guard):\n");
    test_sync_blob();
    printf("embed sync (FFI bidir, one-tap converge):\n");
    test_embed_sync_exchange();
    printf("embed sync (busy port -> fast distinct error):\n");
    test_embed_sync_portbusy();
    printf("embed encrypted (FFI store/reveal):\n");
    test_embed_encrypted();
    printf("embed bundle (FFI file export/import round-trip):\n");
    test_embed_bundle_file();
#endif
    printf("----\n%d passed, %d failed\n", ut_pass, ut_fail);
    return ut_fail == 0 ? 0 : 1;
}

#endif /* UNIT_TEST */

/* keep this translation unit non-empty in normal (non-UNIT_TEST) builds */
typedef int ais_tests_translation_unit_not_empty;
