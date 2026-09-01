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

    /* Cold open with NO hash must land on the timeline, which lists the seeded
     * record. The PWA shipped a "Loading..." hang here because the harness only
     * ever reached views by hash or by setView. */
    ok("cold open (no hash) renders the timeline", cdp_wait_bool(c, HIT, 5000) == 0);
    ok("no stuck Loading...", cdp_wait_bool(c,
        "document.getElementById('out').innerText.indexOf('Loading')<0", 3000) == 0);

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

    /* The escape hatch: whoever arrived here by mistake gets what they MEANT --
     * an untag, not a delete. It runs the same reversible undo window, so cancel
     * it afterwards and confirm the records are all still there. */
    cdp_eval_bool(c, "(function(){document.getElementById('dskeep').click();return true})()", &(int){0});
    ok("the escape hatch closes the delete sheet", cdp_wait_bool(c,
        "document.getElementById('dsheet').hidden===true", 3000) == 0);
    ok("it untags instead of deleting", cdp_wait_bool(c,
        "!document.getElementById('toast').hidden"
        "&&document.getElementById('toast').innerText.indexOf('Removed tag')>=0", 5000) == 0);
    cdp_eval_bool(c, "(function(){delUndo();return true})()", &(int){0});   /* take it back */
    ok("Undo leaves everything as it was",
       cdp_eval_bool(c, "(function(){window.__k=null;fetch('/api/get?keys=venice')"
                        ".then(function(r){return r.text()}).then(function(t){window.__k=t});return true})()",
                     &(int){0}) == 0 &&
       cdp_wait_bool(c, "window.__k!==null&&window.__k.indexOf('example.org/venice')>=0", 5000) == 0);

    /* ---- the edit sheet -------------------------------------------------
     * The keys editor used to be a prompt() asking the user to compose a
     * "-KEY" delta by hand -- and prompt() is silently disabled in an installed
     * PWA, so on the app page it did nothing at all. Drive the real thing:
     * open it, drop a key, add a key, save, and assert the DELTA landed. */
    cdp_eval_bool(c, "(function(){openEdit(2,'https://example.org/canal');return true})()", &(int){0});
    ok("edit sheet opens", cdp_wait_bool(c,
        "!document.getElementById('editsheet').hidden", 5000) == 0);
    ok("it prefills the value", cdp_wait_bool(c,
        "document.getElementById('edval').value==='https://example.org/canal'", 3000) == 0);
    /* the record's CURRENT keys as chips -- not a blank box to retype */
    ok("it shows the current keys as chips", cdp_wait_bool(c,
        "document.getElementById('edchips').children.length===2", 3000) == 0);
    ok("the chips are the record's keys", cdp_wait_bool(c,
        "document.getElementById('edchips').innerText.indexOf('venice')>=0"
        "&&document.getElementById('edchips').innerText.indexOf('trip')>=0", 3000) == 0);

    /* drop 'trip' by its chip button, and add 'canal' through the input */
    cdp_eval_bool(c, "(function(){var c=document.getElementById('edchips');"
                     "for(var i=0;i<c.children.length;i++)"
                     "if(c.children[i].textContent.indexOf('trip')===0){c.children[i].querySelector('button').click();return true}"
                     "return false})()", &(int){0});
    ok("removing a chip drops that key", cdp_wait_bool(c,
        "document.getElementById('edchips').children.length===1"
        "&&document.getElementById('edchips').innerText.indexOf('trip')<0", 3000) == 0);
    cdp_eval_bool(c, "(function(){var i=document.getElementById('edtag');i.value='canal';"
                     "i.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true}));return true})()", &(int){0});
    ok("typing a key and pressing Enter adds a chip", cdp_wait_bool(c,
        "document.getElementById('edchips').innerText.indexOf('canal')>=0", 3000) == 0);

    cdp_eval_bool(c, "(function(){document.getElementById('edsave').click();return true})()", &(int){0});
    ok("saving closes the sheet", cdp_wait_bool(c,
        "document.getElementById('editsheet').hidden===true", 5000) == 0);
    /* The delta really reached the engine, in both directions. cdp_eval_bool
     * needs a BOOLEAN, so the fetch parks its answer on window and the wait
     * polls that -- handing it a Promise just evaluates to "not a boolean". */
    ok("the added key now answers",
       cdp_eval_bool(c, "(function(){window.__k=null;fetch('/api/get?keys=canal')"
                        ".then(function(r){return r.text()}).then(function(t){window.__k=t});return true})()",
                     &(int){0}) == 0 &&
       cdp_wait_bool(c, "window.__k!==null&&window.__k.indexOf('example.org/canal')>=0", 5000) == 0);
    ok("the removed key no longer answers",
       cdp_eval_bool(c, "(function(){window.__k=null;fetch('/api/get?keys=trip')"
                        ".then(function(r){return r.text()}).then(function(t){window.__k=t});return true})()",
                     &(int){0}) == 0 &&
       cdp_wait_bool(c, "window.__k!==null&&window.__k.indexOf('example.org/canal')<0", 5000) == 0);
    /* and the record kept the key that was left alone */
    ok("the untouched key still answers",
       cdp_eval_bool(c, "(function(){window.__k=null;fetch('/api/get?keys=venice')"
                        ".then(function(r){return r.text()}).then(function(t){window.__k=t});return true})()",
                     &(int){0}) == 0 &&
       cdp_wait_bool(c, "window.__k!==null&&window.__k.indexOf('example.org/canal')>=0", 5000) == 0);

    /* the other half of the sheet: editing the VALUE in place (/api/setvalue),
     * which keeps the record's id and its timeline slot */
    cdp_eval_bool(c, "(function(){openEdit(2,'https://example.org/canal');return true})()", &(int){0});
    ok("edit sheet reopens", cdp_wait_bool(c,
        "!document.getElementById('editsheet').hidden", 5000) == 0);
    cdp_eval_bool(c, "(function(){document.getElementById('edval').value='https://example.org/canal-EDITED';"
                     "document.getElementById('edsave').click();return true})()", &(int){0});
    ok("the edited value replaced the old one",
       cdp_eval_bool(c, "(function(){window.__k=null;fetch('/api/get?keys=venice')"
                        ".then(function(r){return r.text()}).then(function(t){window.__k=t});return true})()",
                     &(int){0}) == 0 &&
       cdp_wait_bool(c, "window.__k!==null&&window.__k.indexOf('canal-EDITED')>=0", 5000) == 0);
    /* in PLACE: the old value is gone, and the record kept its id (2) */
    ok("the old value is gone", cdp_wait_bool(c,
        "window.__k.indexOf('example.org/canal\\n')<0", 3000) == 0);
    ok("the record kept its id", cdp_wait_bool(c,
        "window.__k.indexOf('2|https://example.org/canal-EDITED')>=0", 3000) == 0);

    /* UNTAG for real: every earlier tag assertion either declined the confirm or
     * only checked the wording, so the safe action's actual EFFECT -- the tag
     * goes, the records stay -- was never driven from a front end. */
    cdp_eval_bool(c, "(function(){location.hash='#tags';setView('tags');return true})()", &(int){0});
    ok("tags view shows the added key", cdp_wait_bool(c,
        "document.getElementById('out').innerText.indexOf('canal')>=0", 5000) == 0);
    cdp_eval_bool(c, "(function(){untagKey('canal',1);return true})()", &(int){0});
    /* a reversible action gets an undo WINDOW, not a modal: nothing has been sent
     * to the engine yet, so the Undo has something to undo */
    ok("untag opens an undo window", cdp_wait_bool(c,
        "!document.getElementById('toast').hidden"
        "&&document.getElementById('toast').innerText.indexOf(\"Removed tag 'canal'\")>=0", 5000) == 0);
    ok("it offers Undo", cdp_wait_bool(c,
        "!!document.getElementById('toastundo')", 3000) == 0);
    cdp_eval_bool(c, "(function(){delFlush();return true})()", &(int){0});   /* commit now */
    ok("the tag is gone",
       cdp_eval_bool(c, "(function(){window.__k=null;fetch('/api/get?keys=canal')"
                        ".then(function(r){return r.text()}).then(function(t){window.__k=t});return true})()",
                     &(int){0}) == 0 &&
       cdp_wait_bool(c, "window.__k!==null&&window.__k.indexOf('canal-EDITED')<0", 5000) == 0);
    /* the whole point: the RECORD survived, still filed under its other key */
    ok("but the record survived under its other key",
       cdp_eval_bool(c, "(function(){window.__k=null;fetch('/api/get?keys=venice')"
                        ".then(function(r){return r.text()}).then(function(t){window.__k=t});return true})()",
                     &(int){0}) == 0 &&
       cdp_wait_bool(c, "window.__k!==null&&window.__k.indexOf('canal-EDITED')>=0", 5000) == 0);

    /* ---- documents -----------------------------------------------------
     * A multi-line value is stored as one aisdoc:<base64> record, so the line
     * split cannot tear it apart. The page has to DECODE it: printing the marker
     * raw is what the app page used to do, and base64 is not a note. */
    cdp_eval_bool(c, "(function(){window.__d=null;fetch('/api/put?keys=notes',"
                     "{method:'POST',body:'line one\\nline two\\nline three'})"
                     ".then(function(r){return r.text()}).then(function(t){window.__d=t});return true})()",
                  &(int){0});
    ok("a multi-line note is stored", cdp_wait_bool(c,
        "window.__d!==null&&window.__d.indexOf('saved 1')>=0", 5000) == 0);
    cdp_eval_bool(c, "(function(){setView('timeline');return true})()", &(int){0});
    ok("a document renders its text, not its base64", cdp_wait_bool(c,
        "document.getElementById('out').innerText.indexOf('line one')>=0", 5000) == 0);
    ok("every line survives", cdp_wait_bool(c,
        "document.getElementById('out').innerText.indexOf('line three')>=0", 3000) == 0);
    ok("the marker never reaches the screen", cdp_wait_bool(c,
        "document.getElementById('out').innerText.indexOf('aisdoc:')<0", 3000) == 0);

    /* ---- editing the document ------------------------------------------
     * The editor opens on the FULL decoded text (never the aisdoc: marker or
     * the blobs/ path -- /api/doc supplies the body, since the row's preview
     * is bounded), newlines survive the save, and the same record shows the
     * new text after. */
    ok("edit opens on the note's decoded text", cdp_eval_bool(c,
        "(function(){var h=document.querySelectorAll('.hit');"
        "for(var i=0;i<h.length;i++){if(h[i].innerText.indexOf('line two')>=0){"
        "var bs=h[i].querySelectorAll('button');"
        "for(var j=0;j<bs.length;j++)if(bs[j].textContent==='edit'){bs[j].click();return true}}}"
        "return false})()", &(int){0}) == 0 &&
        cdp_wait_bool(c,
        "!document.getElementById('editsheet').hidden"
        "&&document.getElementById('edval').value==='line one\\nline two\\nline three'",
        5000) == 0);
    cdp_eval_bool(c, "(function(){document.getElementById('edval').value="
                     "'line one\\nline 2 EDITED\\nline three\\nline four';"
                     "edSave();return true})()", &(int){0});
    ok("the edited note renders its new lines", cdp_wait_bool(c,
        "document.getElementById('out').innerText.indexOf('line 2 EDITED')>=0"
        "&&document.getElementById('out').innerText.indexOf('line four')>=0", 5000) == 0);
    ok("still as text, not base64", cdp_wait_bool(c,
        "document.getElementById('out').innerText.indexOf('aisdoc:')<0", 3000) == 0);
    ok("and it is still ONE record",
       cdp_eval_bool(c, "(function(){window.__d2=null;fetch('/api/get?keys=notes')"
                        ".then(function(r){return r.text()}).then(function(t){window.__d2=t});return true})()",
                     &(int){0}) == 0 &&
       cdp_wait_bool(c, "window.__d2!==null&&window.__d2.split('\\n')"
                        ".filter(function(s){return s.length}).length===1", 5000) == 0);

    /* ---- secrets -------------------------------------------------------
     * An encrypted record is opaque until a passphrase is given. Encryption is
     * server-side, so if the crypto module is not built there is nothing to
     * drive -- say so rather than fail a build that never had the feature. */
    cdp_eval_bool(c, "(function(){window.__e=null;fetch('/api/put?keys=wifi&enc=1',"
                     "{method:'POST',body:'pw123\\nwifi-Staff-2026'})"
                     ".then(function(r){return r.text()}).then(function(t){window.__e=t});return true})()",
                  &(int){0});
    cdp_wait_bool(c, "window.__e!==null", 5000);
    int haveEnc = 0;
    cdp_eval_bool(c, "window.__e!==null&&window.__e.indexOf('saved 1')>=0", &haveEnc);
    if (!haveEnc) {
        printf("  skip secrets (crypto module not built)\n");
    } else {
        cdp_eval_bool(c, "(function(){setView('timeline');return true})()", &(int){0});
        ok("a secret says it is encrypted", cdp_wait_bool(c,
            "document.getElementById('out').innerText.indexOf('encrypted')>=0", 5000) == 0);
        ok("its ciphertext never reaches the screen", cdp_wait_bool(c,
            "document.getElementById('out').innerText.indexOf('aisc:')<0", 3000) == 0);
        /* the row offers Reveal, and Reveal asks for the passphrase INLINE:
         * prompt() is disabled in an installed PWA, so a prompt would do nothing */
        ok("Reveal opens a passphrase field", cdp_eval_bool(c,
            "(function(){var h=document.querySelectorAll('.hit');"
            "for(var i=0;i<h.length;i++){var b=h[i].querySelector('button');"
            "if(b&&b.textContent==='Reveal'){b.click();return true}}return false})()",
            &(int){0}) == 0 &&
            cdp_wait_bool(c, "!!document.querySelector('.hit input[type=password]')", 3000) == 0);
        ok("the right passphrase shows the cleartext", cdp_eval_bool(c,
            "(function(){var p=document.querySelector('.hit input[type=password]');"
            "p.value='pw123';var bs=p.parentNode.querySelectorAll('button');"
            "for(var i=0;i<bs.length;i++)if(bs[i].textContent==='Show'){bs[i].click();return true}"
            "return false})()", &(int){0}) == 0 &&
            cdp_wait_bool(c, "document.getElementById('out').innerText.indexOf('wifi-Staff-2026')>=0",
                          5000) == 0);
        /* and offers to copy what was revealed -- the ciphertext row has no copy */
        ok("the revealed value can be copied", cdp_wait_bool(c,
            "document.getElementById('out').innerText.indexOf('copy')>=0", 3000) == 0);

        /* ---- saving an encrypted record ---------------------------------
         * The refusal first: a ticked Encrypt with no passphrase must NOT fall
         * back to saving in the clear. alert() is stubbed so the modal cannot
         * block the driver, and so its wording can be asserted. */
        cdp_eval_bool(c, "(function(){window.__alert=null;window.alert=function(m){window.__alert=m};"
                         "openSheet();document.getElementById('v').value='safe-combo-1234';"
                         "document.getElementById('vk').value='locker';"
                         "var e=document.getElementById('enc');e.checked=true;"
                         "e.dispatchEvent(new Event('change'));"
                         "document.getElementById('save').click();return true})()", &(int){0});
        ok("Encrypt reveals the passphrase field", cdp_wait_bool(c,
            "document.getElementById('pp').hidden===false", 3000) == 0);
        ok("and its confirmation field", cdp_wait_bool(c,
            "document.getElementById('pp2').hidden===false", 3000) == 0);
        ok("and says a lost passphrase is gone", cdp_wait_bool(c,
            "document.getElementById('ppnote').hidden===false"
            "&&document.getElementById('ppnote').textContent==='A lost passphrase cannot be recovered.'",
            3000) == 0);
        ok("no passphrase is refused, in those words", cdp_wait_bool(c,
            "window.__alert==='Enter a passphrase to encrypt'", 3000) == 0);
        ok("the sheet stays open to retry", cdp_wait_bool(c,
            "document.getElementById('sheet').hidden===false", 3000) == 0);
        ok("and nothing was saved in the clear",
           cdp_eval_bool(c, "(function(){window.__k=null;fetch('/api/get?keys=locker')"
                            ".then(function(r){return r.text()}).then(function(t){window.__k=t});return true})()",
                         &(int){0}) == 0 &&
           cdp_wait_bool(c, "window.__k!==null&&window.__k.indexOf('safe-combo-1234')<0", 5000) == 0);

        /* Mismatch blocks the save. The second value differs by a trailing
         * space only: the comparison must be exact, no trimming, because the
         * engine would encrypt under whatever #pp holds. */
        cdp_eval_bool(c, "(function(){window.__alert=null;"
                         "document.getElementById('pp').value='pw123';"
                         "document.getElementById('pp2').value='pw123 ';"
                         "document.getElementById('save').click();return true})()", &(int){0});
        ok("mismatched passphrases are refused, in those words", cdp_wait_bool(c,
            "window.__alert==='Passphrases do not match'", 3000) == 0);
        ok("the mismatch keeps the sheet open", cdp_wait_bool(c,
            "document.getElementById('sheet').hidden===false", 3000) == 0);
        ok("and saved nothing",
           cdp_eval_bool(c, "(function(){window.__k=null;fetch('/api/get?keys=locker')"
                            ".then(function(r){return r.text()}).then(function(t){window.__k=t});return true})()",
                         &(int){0}) == 0 &&
           cdp_wait_bool(c, "window.__k!==null&&window.__k.indexOf('safe-combo-1234')<0"
                            "&&window.__k.indexOf('aisc:')<0", 5000) == 0);

        cdp_eval_bool(c, "(function(){document.getElementById('pp2').value='pw123';"
                         "document.getElementById('save').click();return true})()", &(int){0});
        ok("matching passphrases save and close", cdp_wait_bool(c,
            "document.getElementById('sheet').hidden===true", 5000) == 0);
        /* what landed is the marker, not the value: the whole point of the box */
        ok("the stored value is encrypted, not the cleartext",
           cdp_eval_bool(c, "(function(){window.__k=null;fetch('/api/get?keys=locker')"
                            ".then(function(r){return r.text()}).then(function(t){window.__k=t});return true})()",
                         &(int){0}) == 0 &&
           cdp_wait_bool(c, "window.__k!==null&&window.__k.indexOf('aisc:')>=0"
                            "&&window.__k.indexOf('safe-combo-1234')<0", 5000) == 0);
    }

    /* ---- value-ranked search + tag autocomplete -------------------------
     * The search's second half: records whose VALUE holds the query rank
     * after the tag matches, under one "matched in the value" separator,
     * deduped by id, and the value match is case-insensitive end to end.
     * Seed three records over the API: one findable only by a value word,
     * one matching a query both ways, one matching that query by value only. */
    cdp_eval_bool(c, "(function(){window.__f1=null;fetch('/api/put?keys=contacts',"
                     "{method:'POST',body:'call the plumber Mario'})"
                     ".then(function(r){return r.text()}).then(function(t){window.__f1=t});return true})()",
                  &(int){0});
    cdp_wait_bool(c, "window.__f1!==null", 5000);
    cdp_eval_bool(c, "(function(){window.__f2=null;fetch('/api/put?keys=gelato',"
                     "{method:'POST',body:'gelato place in dorsoduro'})"
                     ".then(function(r){return r.text()}).then(function(t){window.__f2=t});return true})()",
                  &(int){0});
    cdp_wait_bool(c, "window.__f2!==null", 5000);
    cdp_eval_bool(c, "(function(){window.__f3=null;fetch('/api/put?keys=food',"
                     "{method:'POST',body:'best gelato in rome'})"
                     ".then(function(r){return r.text()}).then(function(t){window.__f3=t});return true})()",
                  &(int){0});
    cdp_wait_bool(c, "window.__f3!==null", 5000);

    /* (a) a word that is in the value but in NO tag still finds the record,
     * listed after the separator; nothing sits before the separator */
    cdp_eval_bool(c, "(function(){document.getElementById('q').value='plumber';"
                     "setView('recall');return true})()", &(int){0});
    ok("a value-only word finds its record", cdp_wait_bool(c,
        "document.getElementById('out').innerText.indexOf('plumber Mario')>=0", 5000) == 0);
    ok("it renders after the value separator", cdp_wait_bool(c,
        "!!document.getElementById('vsep')"
        "&&document.getElementById('out').innerText.indexOf('matched in the value')"
        "<document.getElementById('out').innerText.indexOf('plumber Mario')", 3000) == 0);
    ok("no tag matched, so no rows precede the separator", cdp_wait_bool(c,
        "document.getElementById('out').querySelectorAll('.hit').length===1", 3000) == 0);
    ok("the count line is the value half's", cdp_wait_bool(c,
        "document.getElementById('count').textContent.indexOf('1 result')>=0", 3000) == 0);
    /* the Add sheet meets its prefill: the searched word matched no tag, so
     * saving would file it under an unseen junk tag -- focus lands on the
     * tag field, not the value */
    cdp_eval_bool(c, "(function(){openSheet();return true})()", &(int){0});
    ok("an unmatched prefilled tag gets the focus", cdp_wait_bool(c,
        "document.activeElement&&document.activeElement.id==='vk'", 3000) == 0);
    cdp_eval_bool(c, "(function(){closeSheet();return true})()", &(int){0});
    cdp_eval_bool(c, "(function(){document.getElementById('q').value='PLUMBER';"
                     "setView('recall');return true})()", &(int){0});
    ok("and the value match is case-insensitive", cdp_wait_bool(c,
        "document.getElementById('out').innerText.indexOf('plumber Mario')>=0", 5000) == 0);

    /* (b) a query matching both ways: the tag match stays before the
     * separator, the value-only record lands after it, and the record that
     * matched both ways is listed exactly once */
    cdp_eval_bool(c, "(function(){document.getElementById('q').value='gelato';"
                     "setView('recall');return true})()", &(int){0});
    ok("the tag match renders before the separator", cdp_wait_bool(c,
        "(function(){var t=document.getElementById('out').innerText;"
        "return t.indexOf('dorsoduro')>=0&&t.indexOf('matched in the value')>t.indexOf('dorsoduro')})()",
        5000) == 0);
    ok("the value-only record renders after it", cdp_wait_bool(c,
        "(function(){var t=document.getElementById('out').innerText;"
        "return t.indexOf('best gelato in rome')>t.indexOf('matched in the value')})()", 3000) == 0);
    ok("the both-ways record is not duplicated", cdp_wait_bool(c,
        "document.getElementById('out').innerText.split('dorsoduro').length===2", 3000) == 0);
    ok("the count names both halves", cdp_wait_bool(c,
        "document.getElementById('count').textContent"
        ".indexOf('1 result + 1 in the value')>=0", 3000) == 0);
    cdp_eval_bool(c, "(function(){openSheet();return true})()", &(int){0});
    ok("a tag-matched prefill keeps focus on the value", cdp_wait_bool(c,
        "document.activeElement&&document.activeElement.id==='v'", 3000) == 0);
    cdp_eval_bool(c, "(function(){closeSheet();return true})()", &(int){0});

    /* (c) tag autocomplete under the search field: typing a prefix of an
     * existing tag offers a chip; tapping it completes the token (plus a
     * trailing space) and re-runs the search */
    cdp_eval_bool(c, "(function(){var q=document.getElementById('q');q.value='';q.focus();"
                     "return true})()", &(int){0});
    cdp_insert_text(c, "gela");
    ok("typing a prefix offers the tag chip", cdp_wait_bool(c,
        "(function(){var r=document.getElementById('qsuggest');"
        "for(var i=0;i<r.children.length;i++)if(r.children[i].textContent==='gelato')return true;"
        "return false})()", 5000) == 0);
    ok("tapping it completes the token and searches", cdp_eval_bool(c,
        "(function(){var r=document.getElementById('qsuggest');"
        "for(var i=0;i<r.children.length;i++)if(r.children[i].textContent==='gelato'){r.children[i].click();return true}"
        "return false})()", &(int){0}) == 0 &&
        cdp_wait_bool(c, "document.getElementById('q').value==='gelato '", 3000) == 0 &&
        cdp_wait_bool(c, "document.getElementById('out').innerText.indexOf('dorsoduro')>=0", 5000) == 0);
    /* the same row sits under the Add and Edit tag fields */
    ok("the Add sheet carries a suggestion row", cdp_wait_bool(c,
        "!!document.getElementById('vksuggest')", 3000) == 0);
    ok("the Edit sheet carries a suggestion row", cdp_wait_bool(c,
        "!!document.getElementById('edsuggest')", 3000) == 0);

    cdp_close(c);
    printf("cdp: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
