/* tests.c -- AIS regression bundle. Linear, inline, one comment per test.
 *
 * Compiled only under -DUNIT_TEST (see `make ut`); invisible to the normal
 * build. Independent black-box tests of the ais.h contract plus a few pure
 * helpers (key.h). Mutating tests use a fresh scratch INDEX under /tmp, wiped
 * before and after, so the suite is idempotent and never touches the committed
 * fixture tests/INDEX/store.
 */
#ifdef UNIT_TEST

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ais.h"
#include "key.h"
#include "post.h"
#include "store.h"
#include "compact.h"
#include "stats.h"
#include "find.h"
#include "doc.h"
#include "embed.h"

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

    /* a detach is durable through compaction: store line rewritten without the
     * key, ktomb cleared, recall stays empty, posting stays gone */
    CHECK(ais_update(&a, id, "-italy") == 0, "update: detach 'italy' before compact");
    CHECK(ais_compact(&a) == 0, "update: compact ok");
    query(&a, AIS_AND, &v, 1, "italy");  CHECK(v.n == 0, "update: 'italy' empty after compact");
    query(&a, AIS_AND, &v, 1, "venice"); { long w[1] = {1}; CHECK(ids_eq(&v, w, 1), "update: 'venice' survives compact"); }
    CHECK(ktomb_active(&a) == 0, "update: ktomb cleared by compact");
    CHECK(read_posting(dir, "italy", vids, 16, &asc) == -1, "update: 'italy' posting gone after compact");

    /* a deleted record cannot be updated */
    ais_del(&a, id);
    CHECK(ais_update(&a, id, "rome") == -1, "update: deleted id -> -1");

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

int main(void)
{
    printf("AIS regression tests (make ut)\n");
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
    printf("recovery:\n");
    test_next_id_recovery();
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
    printf("put_value (one paste -> one record):\n");
    test_put_value();
    printf("embed (FFI seam):\n");
    test_embed();
    printf("----\n%d passed, %d failed\n", ut_pass, ut_fail);
    return ut_fail == 0 ? 0 : 1;
}

#endif /* UNIT_TEST */

/* keep this translation unit non-empty in normal (non-UNIT_TEST) builds */
typedef int ais_tests_translation_unit_not_empty;
