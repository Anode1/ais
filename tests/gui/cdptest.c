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
    /* 2 records under 'venice', one holding two links: recall lists LINKS, so it
     * says 3 here. The delete modal counts RECORDS and says 2 -- see below. */
    ok("count shows the results", cdp_wait_bool(c,
        "document.getElementById('count').textContent.indexOf('3 result')>=0", 3000) == 0);

    /* ---- the two tag-level actions ------------------------------------
     * They sit one tap apart and are opposite in consequence, so the guard on
     * the destructive one is driven for real: open it, confirm it ships
     * disabled, type a WRONG name (still disabled), type the right one (now
     * enabled), then take the escape hatch instead and assert nothing died. */
    cdp_eval_bool(c, "(function(){location.hash='#tags';setView('tags');return true})()", &(int){0});
    ok("tags view lists the tag", cdp_wait_bool(c,
        "document.getElementById('out').innerText.indexOf('venice')>=0", 5000) == 0);
    /* the labels are the defence: safe one names the TAG, destructive one the RECORDS */
    ok("safe action is labelled 'Remove tag'", cdp_wait_bool(c,
        "document.getElementById('out').innerText.indexOf('Remove tag')>=0", 3000) == 0);
    /* the tag badge and the destructive label both count RECORDS (2), not the 3
     * links recall shows -- a destructive label must match what actually goes */
    ok("destructive action names the records", cdp_wait_bool(c,
        "document.getElementById('out').innerText.indexOf('Delete 2 records')>=0", 3000) == 0);

    cdp_eval_bool(c, "(function(){openDelUnder('venice');return true})()", &(int){0});
    ok("delete sheet opens", cdp_wait_bool(c,
        "!document.getElementById('dsheet').hidden", 5000) == 0);
    ok("its title names the records, not the tag", cdp_wait_bool(c,
        "document.getElementById('dstitle').textContent==='Delete 2 records?'", 3000) == 0);
    ok("it says the records go, not the tag", cdp_wait_bool(c,
        "document.querySelector('#dsheet .lead').textContent.indexOf('not the tag')>=0", 3000) == 0);
    ok("it warns about the OTHER tags", cdp_wait_bool(c,
        "document.getElementById('dsbody').textContent.indexOf('every other tag')>=0", 3000) == 0);
    ok("it previews what would go", cdp_wait_bool(c,
        "document.getElementById('dsprev').children.length===2", 3000) == 0);
    /* /api/get emits one line per LINK; one of these records holds two. Counting
     * lines said "Delete 3 records" beside a tag badge reading 2. */
    ok("a multi-link record counts ONCE", cdp_wait_bool(c,
        "document.getElementById('dsprev').innerText.indexOf('+1 more link')>=0", 3000) == 0);
    ok("confirm starts DISABLED", cdp_wait_bool(c,
        "document.getElementById('dsgo').disabled===true", 3000) == 0);

    cdp_eval_bool(c, "(function(){var i=document.getElementById('dsname');i.focus();return true})()", &(int){0});
    cdp_insert_text(c, "venise");                       /* one letter off */
    ok("a WRONG name leaves it disabled", cdp_wait_bool(c,
        "document.getElementById('dsname').value==='venise'&&document.getElementById('dsgo').disabled===true", 3000) == 0);
    cdp_eval_bool(c, "(function(){var i=document.getElementById('dsname');i.value='venice';"
                     "i.dispatchEvent(new Event('input'));return true})()", &(int){0});
    ok("the exact name enables it", cdp_wait_bool(c,
        "document.getElementById('dsgo').disabled===false", 3000) == 0);

    /* the escape hatch: the user who arrived here by mistake gets what they meant */
    cdp_eval_bool(c, "(function(){window.confirm=function(){return false};"
                     "document.getElementById('dskeep').click();return true})()", &(int){0});
    ok("the escape hatch closes the delete sheet", cdp_wait_bool(c,
        "document.getElementById('dsheet').hidden===true", 3000) == 0);
    ok("and nothing was deleted", cdp_wait_bool(c,
        "document.getElementById('out').innerText.indexOf('venice')>=0", 5000) == 0);

    cdp_close(c);
    printf("cdp: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
