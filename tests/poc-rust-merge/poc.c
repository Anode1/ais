/* poc.c -- harness for the Rust merge PoC. Reads N sorted posting-list files
 * (idx/<p>/<key>: decimal ids, one per line, ascending), intersects them with
 * the Rust ais_merge_ids AND with a C reference, and asserts the outputs are
 * identical. That checks the Rust drop-in against merge.c's semantics on real
 * index data.
 *
 * Built two ways (see Makefile): linked with the Rust staticlib (poc), and with
 * a C definition of ais_merge_ids instead (poc_c) so the binary sizes can be
 * compared apples to apples. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const long *ids; long len; } ais_stream; /* matches Rust repr(C) */

/* C reference k-way AND, mirrors c/merge.c. */
static long c_merge_and(const ais_stream *s, int n, long *out, long cap)
{
    long *cur = calloc(n, sizeof *cur), count = 0, last = 0;
    int have_last = 0, i;
    for (;;) {
        long min = 0; int found = 0;
        for (i = 0; i < n; i++)
            if (cur[i] < s[i].len) {
                long h = s[i].ids[cur[i]];
                if (!found || h < min) { min = h; found = 1; }
            }
        if (!found) break;
        int all = 1;
        for (i = 0; i < n; i++)
            if (!(cur[i] < s[i].len && s[i].ids[cur[i]] == min)) { all = 0; break; }
        if (all && !(have_last && min == last)) {
            if (count < cap) out[count] = min;
            count++; last = min; have_last = 1;
        }
        for (i = 0; i < n; i++)
            if (cur[i] < s[i].len && s[i].ids[cur[i]] == min) cur[i]++;
    }
    free(cur);
    return count;
}

#ifdef BASELINE
/* pure-C build: ais_merge_ids is just the C reference, no Rust linked. */
long ais_merge_ids(const ais_stream *s, int n, int mode, long *out, long cap)
{
    (void)mode; return c_merge_and(s, n, out, cap);
}
#else
extern long ais_merge_ids(const ais_stream *s, int n, int mode, long *out, long cap);
#endif

static long *read_postings(const char *path, long *n_out)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }
    long cap = 1024, n = 0, *a = malloc(cap * sizeof *a), v;
    while (fscanf(f, "%ld", &v) == 1) {
        if (n == cap) { cap *= 2; a = realloc(a, cap * sizeof *a); }
        a[n++] = v;
    }
    fclose(f);
    *n_out = n;
    return a;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <posting1> <posting2> [...]\n", argv[0]);
        return 2;
    }
    int n = argc - 1, i;
    ais_stream *s = calloc(n, sizeof *s);
    long total = 0;
    for (i = 0; i < n; i++) {
        long len;
        s[i].ids = read_postings(argv[i + 1], &len);
        s[i].len = len;
        total += len;
    }
    long cap = total ? total : 1, *ro = malloc(cap * sizeof *ro), *co = malloc(cap * sizeof *co);
    long rc = ais_merge_ids(s, n, 0, ro, cap);       /* Rust (or C in baseline) */
    long cc = c_merge_and(s, n, co, cap);            /* C reference */
    int match = (rc == cc) && (rc <= cap) && (memcmp(ro, co, (size_t)rc * sizeof *ro) == 0);
    printf("streams=%d  merge_and=%ld  c_ref=%ld  match=%s\n",
           n, rc, cc, match ? "YES" : "NO");
    return match ? 0 : 1;
}
