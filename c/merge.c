/* merge.c -- k-way streaming merge (AND/OR). See merge.h. */
#include "compact.h"
#include "merge.h"

/* Smallest live head across the streams. Returns 1 and sets *min if any
 * stream is alive, else 0. */
static int merge_min_head(const post_stream *s, int n, long *min)
{
    int i, found = 0;

    for (i = 0; i < n; i++) {
        if (!s[i].alive)
            continue;
        if (!found || s[i].head < *min) {
            *min = s[i].head;
            found = 1;
        }
    }
    return found;
}

/* Emit ID unless tombstoned. Sets *stop to the callback's stop code (negative)
 * if it asks to stop. Returns 0 on success, -1 on error. */
static int merge_emit(ais *a, long id, ais_id_cb cb, void *ctx, int *stop)
{
    int t = tomb_contains(a, id);

    *stop = 0;
    if (t < 0)
        return -1;
    if (t == 1)
        return 0;   /* suppressed */
    *stop = cb(id, ctx);
    return 0;
}

int merge_run(ais *a, post_stream *streams, int nstreams, ais_mode mode,
              ais_id_cb cb, void *ctx)
{
    long last = 0;        /* last id emitted; guards against duplicates ... */
    int  have_last = 0;   /* ... within a stream (e.g. a hand-edited index) */

    if (nstreams <= 0)
        return 0;

    for (;;) {
        long min = 0;   /* set by merge_min_head below; init silences -Wmaybe-uninitialized under inlining */
        int i, all, stop, emit;

        if (!merge_min_head(streams, nstreams, &min))
            break;   /* every stream exhausted */

        emit = !(have_last && min == last);

        if (mode == AIS_AND) {
            /* min must be present in every stream to survive the intersection */
            all = 1;
            for (i = 0; i < nstreams; i++) {
                if (!streams[i].alive || streams[i].head != min) {
                    all = 0;
                    break;
                }
            }
            if (all && emit) {
                if (merge_emit(a, min, cb, ctx, &stop) != 0)
                    return -1;
                last = min;
                have_last = 1;
                if (stop != 0)
                    return stop;
            }
            /* advance every stream sitting at the minimum */
            for (i = 0; i < nstreams; i++)
                if (streams[i].alive && streams[i].head == min)
                    if (post_next(&streams[i]) != 0)
                        return -1;
        } else {   /* AIS_OR */
            if (emit) {
                if (merge_emit(a, min, cb, ctx, &stop) != 0)
                    return -1;
                last = min;
                have_last = 1;
                if (stop != 0)
                    return stop;
            }
            /* dedup: advance every stream at the emitted minimum */
            for (i = 0; i < nstreams; i++)
                if (streams[i].alive && streams[i].head == min)
                    if (post_next(&streams[i]) != 0)
                        return -1;
        }
    }
    return 0;
}
