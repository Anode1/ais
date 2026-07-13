/* cdptest.c -- the click-and-assert interaction test for `ais --serve`, driven
 * through the C CDP client (cdp.c). Unlike ui.sh (static dump-dom: the controls
 * exist), this exercises the real input+fetch path: focus the search box, TYPE
 * a key, press Enter, and assert the seeded record renders in the results.
 *
 * The server and Chrome are started by inter.sh; this program only attaches.
 *   argv: cdptest CHROME_HOST CHROME_PORT SERVER_URL
 * Exit 0 = all passed, 1 = a failure, 2 = usage. */
#include "cdp.h"
#include <stdio.h>
#include <stdlib.h>

static int pass = 0, fail = 0;
static void ok(const char *label, int cond) {
    if (cond) { pass++; printf("  ok   %s\n", label); }
    else      { fail++; printf("  FAIL %s\n", label); }
}

/* the record inter.sh seeds under key "venice" */
#define SEEDED "example.org/venice"
#define HIT    "document.getElementById('out').innerText.indexOf('" SEEDED "')>=0"

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);   /* line-buffered: visible under a pipe */
    if (argc < 4) { fprintf(stderr, "usage: %s CHROME_HOST CHROME_PORT SERVER_URL\n", argv[0]); return 2; }
    const char *host = argv[1]; int port = atoi(argv[2]); const char *url = argv[3];

    cdp *c = cdp_open(host, port);
    if (!c) { fprintf(stderr, "cdptest: cannot attach to Chrome at %s:%s\n", host, argv[2]); return 1; }

    ok("navigate to --serve page", cdp_navigate(c, url) == 0);
    ok("page loaded (#q present)", cdp_wait_bool(c, "!!document.getElementById('q')", 5000) == 0);

    /* The page now OPENS on Recent (which lists every record), so switch to the
     * empty Search view first to establish the "absent until queried" baseline. */
    cdp_eval_bool(c, "(function(){setView('recall');return true})()", &(int){0});
    int before = -1;
    cdp_wait_bool(c, "!" HIT, 3000);            /* let the recall view settle empty */
    cdp_eval_bool(c, HIT, &before);
    ok("record absent before query (control)", before == 0);

    int typed = cdp_eval_bool(c, "(function(){document.getElementById('q').focus();return true})()", &(int){0}) == 0
             && cdp_insert_text(c, "venice") == 0
             && cdp_wait_bool(c, "document.getElementById('q').value==='venice'", 2000) == 0;
    ok("focus + type 'venice' into #q", typed);

    ok("press Enter", cdp_key(c, "Enter") == 0);
    ok("seeded record renders in #out after Enter", cdp_wait_bool(c, HIT, 5000) == 0);
    ok("count shows the result", cdp_wait_bool(c,
        "document.getElementById('count').textContent.indexOf('1 result')>=0", 3000) == 0);

    cdp_close(c);
    printf("cdp: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
