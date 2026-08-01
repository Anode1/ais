/* ffi_stack.c -- how much STACK the engine actually needs at the FFI seam.
 *
 * The Flutter app calls ais_embed_* from a Dart isolate, not from main(), and
 * runs on a thread stack far smaller than a process's default 8 MB. c/ais.c has
 * long carried a 512 KB figure for it in a comment, and a change that doubled
 * ais_put_at's frame slipped past every test in the suite: nothing measured it.
 * A `ulimit -s` test on the CLI does not measure it either, because main()'s own
 * frame is ~141 KB that the app never pays -- it overstates the need by a third
 * and would fail for the wrong reason.
 *
 * So: run the real embed entry points on a thread whose stack size we set
 * exactly, and report the smallest size that survives. Called by run.sh, which
 * asserts the answer is inside the budget.
 *
 *   ffi_stack <KB> <index-dir> <folder-dir> store|resurrect|folder
 *
 * Exit 0 = it fit, 139/other = it did not (the thread died on the guard page).
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "embed.h"

static const char *g_index, *g_folder, *g_mode;
static int g_rc;

static void *work(void *unused)
{
    void *h;

    (void)unused;
    h = ais_embed_open(g_index);
    if (h == NULL) { fprintf(stderr, "ffi_stack: cannot open %s\n", g_index); g_rc = 3; return NULL; }

    if (strcmp(g_mode, "folder") == 0) {
        /* the app's auto-sync: import a shared folder's bundles. The deepest
         * chain in the engine -- import -> put -> post_keys -> a store seek. */
        g_rc = (ais_embed_sync_folder_force(h, g_folder, 0) < -1) ? 4 : 0;
    } else {
        /* store: the primary save. "resurrect" is the same call against an index
         * whose record is tombstoned, which takes the longer branch. */
        g_rc = (ais_embed_store(h, "alpha beta gamma", "http://x/stacktest") < 0) ? 5 : 0;
    }
    ais_embed_close(h);
    return NULL;
}

int main(int argc, char **argv)
{
    pthread_attr_t at;
    pthread_t t;
    size_t kb;

    if (argc < 5) {
        fprintf(stderr, "usage: %s KB INDEX FOLDER store|resurrect|folder\n", argv[0]);
        return 2;
    }
    kb = (size_t)atol(argv[1]);
    g_index = argv[2];
    g_folder = argv[3];
    g_mode = argv[4];

    pthread_attr_init(&at);
    if (pthread_attr_setstacksize(&at, kb * 1024) != 0) {
        fprintf(stderr, "ffi_stack: %luKB is below the platform minimum\n", (unsigned long)kb);
        return 2;
    }
    if (pthread_create(&t, &at, work, NULL) != 0) {
        fprintf(stderr, "ffi_stack: cannot spawn\n");
        return 2;
    }
    pthread_join(t, NULL);
    pthread_attr_destroy(&at);
    return g_rc;
}
