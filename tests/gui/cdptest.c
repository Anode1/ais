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
/* the value of a boolean expression; cdp_eval_bool's own return says only that it evaluated */
static int truthy(cdp *c, const char *js) { int v = 0; return cdp_eval_bool(c, js, &v) == 0 && v; }
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
    /* the count is the count: no timing figure beside it */
    ok("the count carries no ms figure", cdp_wait_bool(c,
        "document.getElementById('count').textContent==='3 results'", 3000) == 0);

    /* ---- the filter row: two chips under the search field --------------- */
    ok("the filter row holds Any tag and Date", cdp_wait_bool(c,
        "document.getElementById('anyk').tagName==='BUTTON'"
        "&&document.getElementById('anyk').textContent==='Any tag'"
        "&&document.getElementById('datebtn').textContent==='Date'", 3000) == 0);
    cdp_eval_bool(c, "(function(){document.getElementById('anyk').click();return true})()", &(int){0});
    ok("Any tag shows its pressed state", cdp_wait_bool(c,
        "document.getElementById('anyk').getAttribute('aria-pressed')==='true'", 3000) == 0);
    cdp_eval_bool(c, "(function(){document.getElementById('anyk').click();return true})()", &(int){0});
    ok("and releases it", cdp_wait_bool(c,
        "document.getElementById('anyk').getAttribute('aria-pressed')==='false'", 3000) == 0);
    /* each chip shows only on the view it acts on: Any tag on Search, Date on
     * Recent, neither on Tags */
    ok("on Search the Date chip is hidden", cdp_wait_bool(c,
        "view==='recall'&&document.getElementById('anyk').hidden===false"
        "&&document.getElementById('datebtn').parentNode.hidden===true", 3000) == 0);
    cdp_eval_bool(c, "(function(){setView('tags');return true})()", &(int){0});
    ok("on Tags the filter row is hidden", cdp_wait_bool(c,
        "document.getElementById('filters').hidden===true", 3000) == 0);
    cdp_eval_bool(c, "(function(){setView('timeline');return true})()", &(int){0});
    ok("on Recent the Date chip shows and Any tag is hidden", cdp_wait_bool(c,
        "document.getElementById('filters').hidden===false"
        "&&document.getElementById('datebtn').parentNode.hidden===false"
        "&&document.getElementById('anyk').hidden===true", 3000) == 0);
    ok("the date inputs start closed", cdp_wait_bool(c,
        "document.getElementById('tlrange').hidden===true", 3000) == 0);
    cdp_eval_bool(c, "(function(){document.getElementById('datebtn').click();return true})()", &(int){0});
    ok("Date opens the date inputs", cdp_wait_bool(c,
        "document.getElementById('tlrange').hidden===false&&view==='timeline'", 3000) == 0);
    cdp_eval_bool(c, "(function(){var f=document.getElementById('tlfrom'),t=document.getElementById('tlto');"
                     "f.value='2026-08-12';t.value='2026-08-20';t.dispatchEvent(new Event('change'));return true})()",
                  &(int){0});
    ok("a set range names itself on the chip", cdp_wait_bool(c,
        "document.getElementById('datebtn').textContent==='12\u201320 Aug'"
        "&&document.getElementById('tlclear').hidden===false", 3000) == 0);
    cdp_eval_bool(c, "(function(){document.getElementById('tlclear').click();return true})()", &(int){0});
    ok("its clear button resets the chip", cdp_wait_bool(c,
        "document.getElementById('datebtn').textContent==='Date'"
        "&&document.getElementById('tlclear').hidden===true"
        "&&document.getElementById('tlfrom').value===''", 3000) == 0);
    ok("and the timeline lists everything again", cdp_wait_bool(c, HIT, 5000) == 0);

    /* ---- the header menu: the library path only in its dialog ------------ */
    ok("the main view shows no library path", cdp_wait_bool(c,
        "document.getElementById('storesheet').hidden===true"
        "&&document.getElementById('storesheet').contains(document.getElementById('store'))", 3000) == 0);
    ok("the header menu starts closed", cdp_wait_bool(c,
        "document.getElementById('menu').hidden===true", 3000) == 0);
    cdp_eval_bool(c, "(function(){document.getElementById('menubtn').click();return true})()", &(int){0});
    ok("the header menu opens with its three items", cdp_wait_bool(c,
        "(function(){var m=document.getElementById('menu');if(m.hidden)return false;var t=m.innerText;"
        "return t.indexOf('Free up space')>=0&&t.indexOf('Sync & backup')>=0&&t.indexOf('Switch library')>=0})()",
        3000) == 0);
    ok("its button is a 48px target", cdp_wait_bool(c,
        "document.getElementById('menubtn').getBoundingClientRect().height>=48", 3000) == 0);
    cdp_eval_bool(c, "(function(){document.getElementById('storebtn').click();return true})()", &(int){0});
    ok("Switch library shows the path in its dialog", cdp_wait_bool(c,
        "document.getElementById('menu').hidden===true"
        "&&document.getElementById('storesheet').hidden===false"
        "&&document.getElementById('store').textContent.charAt(0)==='/'", 5000) == 0);
    cdp_eval_bool(c, "(function(){document.getElementById('storecancel').click();return true})()", &(int){0});
    ok("and Cancel closes it", cdp_wait_bool(c,
        "document.getElementById('storesheet').hidden===true", 3000) == 0);

    /* ---- the two tag-level actions ------------------------------------
     * They sit one tap apart and are opposite in consequence, so the guard on
     * the destructive one is driven for real: open it, confirm it ships
     * disabled, type a WRONG name (still disabled), type the right one (now
     * enabled), then take the escape hatch instead and assert nothing died. */
    cdp_eval_bool(c, "(function(){location.hash='#tags';setView('tags');return true})()", &(int){0});
    ok("tags view lists the tag", cdp_wait_bool(c,
        "document.getElementById('out').innerText.indexOf('venice')>=0", 5000) == 0);
    /* both tag actions sit behind the row's trailing menu, closed until asked */
#define VROW "(function(){var r=null;[].forEach.call(document.querySelectorAll('.taglink'),function(b){" \
             "if(b.textContent==='venice')r=b.parentNode});return r})()"
    ok("the row's actions start hidden", cdp_wait_bool(c,
        VROW ".querySelector('.pop').hidden===true", 3000) == 0);
    ok("the row is 56px tall", cdp_wait_bool(c,
        VROW ".getBoundingClientRect().height>=56", 3000) == 0);
    cdp_eval_bool(c, "(function(){" VROW ".querySelector('.tagmenu').click();return true})()", &(int){0});
    /* the labels are the defence: safe one names the TAG, destructive one the ITEMS */
    ok("the row menu opens with 'Remove tag'", cdp_wait_bool(c,
        VROW ".querySelector('.pop').hidden===false"
        "&&" VROW ".querySelector('.pop').innerText.indexOf('Remove tag')>=0", 3000) == 0);
    /* the tag badge and the destructive label both count RECORDS (2), not the 3
     * links recall shows: a destructive label must match what actually goes */
    ok("its destructive item names the items", cdp_wait_bool(c,
        VROW ".querySelector('.pop').innerText.indexOf('Delete 2 items')>=0", 3000) == 0);

    /* the destructive item deletes nothing by itself: it opens the typed confirm */
    cdp_eval_bool(c, "(function(){" VROW ".querySelector('.pop .danger').click();return true})()", &(int){0});
    ok("delete sheet opens", cdp_wait_bool(c,
        "!document.getElementById('dsheet').hidden", 5000) == 0);
    ok("and the menu closed behind it", cdp_wait_bool(c,
        VROW ".querySelector('.pop').hidden===true", 3000) == 0);
    ok("its title asks about the items", cdp_wait_bool(c,
        "document.getElementById('dstitle').textContent==='Delete these items?'", 3000) == 0);
    ok("its button counts them", cdp_wait_bool(c,
        "document.getElementById('dsgo').textContent==='Delete 2 items'", 3000) == 0);
    ok("it says the items go, not the tag", cdp_wait_bool(c,
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
    /* The Edit sheet refuses as the Add sheet does: a tag over 255 bytes in
     * the same sentence, before anything is sent, with the sheet kept open;
     * and a server refusal (an empty text) shows the server's sentence. */
    cdp_eval_bool(c, "(function(){window.__alert=null;window.alert=function(m){window.__alert=m};"
                     "var h=document.querySelectorAll('.hit');"
                     "for(var i=0;i<h.length;i++){if(h[i].innerText.indexOf('line 2 EDITED')>=0){"
                     "var bs=h[i].querySelectorAll('button');"
                     "for(var j=0;j<bs.length;j++)if(bs[j].textContent==='edit'){bs[j].click();return true}}}"
                     "return false})()", &(int){0});
    cdp_wait_bool(c, "!document.getElementById('editsheet').hidden", 5000);
    cdp_eval_bool(c, "(function(){document.getElementById('edtag').value=new Array(131).join('\u0436');"
                     "edSave();return true})()", &(int){0});
    ok("an over-long tag in Edit is refused, in those words", cdp_wait_bool(c,
        "window.__alert==='One of your tags is too long (max 255 letters, fewer in other alphabets).'", 3000) == 0);
    ok("and the Edit sheet stays open", cdp_wait_bool(c,
        "!document.getElementById('editsheet').hidden", 3000) == 0);
    cdp_eval_bool(c, "(function(){edTags.pop();edChips();window.__alert=null;"
                     "edOldVal='not-the-old-value';document.getElementById('edval').value='line one\\nline 2 EDITED\\nline three\\nline four\\nline five';"
                     "edSave();return true})()", &(int){0});
    ok("a refused text edit shows the server's sentence", cdp_wait_bool(c,
        "window.__alert==='Couldn\\'t update the text.'", 5000) == 0);
    ok("and keeps the Edit sheet open", cdp_wait_bool(c,
        "!document.getElementById('editsheet').hidden", 3000) == 0);
    cdp_eval_bool(c, "(function(){document.getElementById('editsheet').hidden=true;return true})()", &(int){0});
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
        ok("a secret says it is locked", cdp_wait_bool(c,
            "document.getElementById('out').innerText.indexOf('locked')>=0", 5000) == 0);
        ok("its ciphertext never reaches the screen", cdp_wait_bool(c,
            "document.getElementById('out').innerText.indexOf('aisc:')<0", 3000) == 0);
        /* the row offers Unlock, and Unlock asks for the password INLINE:
         * prompt() is disabled in an installed PWA, so a prompt would do nothing */
        ok("Unlock opens a password field", cdp_eval_bool(c,
            "(function(){var h=document.querySelectorAll('.hit');"
            "for(var i=0;i<h.length;i++){var b=h[i].querySelector('button');"
            "if(b&&b.textContent==='Unlock'){b.click();return true}}return false})()",
            &(int){0}) == 0 &&
            cdp_wait_bool(c, "!!document.querySelector('.hit input[type=password]')", 3000) == 0);
        /* a wrong password says so and keeps the field for another try */
        ok("a wrong password says so and keeps the field", cdp_eval_bool(c,
            "(function(){var p=document.querySelector('.hit input[type=password]');"
            "p.value='nope';var bs=p.parentNode.querySelectorAll('button');"
            "for(var i=0;i<bs.length;i++)if(bs[i].textContent==='Show'){bs[i].click();return true}"
            "return false})()", &(int){0}) == 0 &&
            cdp_wait_bool(c, "document.getElementById('out').innerText.indexOf('Wrong password. Try again.')>=0"
                             "&&!!document.querySelector('.hit input[type=password]')", 5000) == 0);
        ok("the right password shows the cleartext", cdp_eval_bool(c,
            "(function(){var p=document.querySelector('.hit input[type=password]');"
            "p.value='pw123';var bs=p.parentNode.querySelectorAll('button');"
            "for(var i=0;i<bs.length;i++)if(bs[i].textContent==='Show'){bs[i].click();return true}"
            "return false})()", &(int){0}) == 0 &&
            cdp_wait_bool(c, "document.getElementById('out').innerText.indexOf('wifi-Staff-2026')>=0",
                          5000) == 0);
        /* and offers to copy what was revealed -- the ciphertext row has no copy */
        ok("the revealed value can be copied", cdp_wait_bool(c,
            "document.getElementById('out').innerText.indexOf('copy')>=0", 3000) == 0);
        /* Issue #4: unlocking wiped the row, menu included; the cleartext row
         * keeps its menu with edit and delete, and locking again keeps one */
#define SECROW "[].slice.call(document.querySelectorAll('#out .hit')).filter(function(r){return r.innerText.indexOf('wifi-Staff-2026')>=0})[0]"
#define SECROW "[].slice.call(document.querySelectorAll('#out .hit')).filter(function(r){return r.innerText.indexOf('wifi-Staff-2026')>=0})[0]"
        ok("the unlocked row keeps its menu", truthy(c,
            "(function(){var h=" SECROW ";return !!h&&h.querySelectorAll('.act').length===1&&!!h.querySelector('.meta')"
            "&&h.querySelector('.act').textContent.indexOf('edit')>=0&&h.querySelector('.act').textContent.indexOf('delete')>=0})()"));
        cdp_eval_bool(c, "(function(){setView('timeline');return true})()", &(int){0});
        cdp_wait_bool(c, "document.getElementById('out').innerText.indexOf('Unlock')>=0", 3000);
        cdp_eval_bool(c, "(function(){var b=[].slice.call(document.querySelectorAll('#out .hit button')).filter(function(b){return b.textContent==='Unlock'})[0];b.click();"
                         "var x=[].slice.call(document.querySelectorAll('#out .hit button')).filter(function(b){return b.textContent==='cancel'})[0];x.click();return true})()", &(int){0});
        ok("Unlock then cancel leaves one lock and one menu", cdp_wait_bool(c,
            "(function(){var h=[].slice.call(document.querySelectorAll('#out .hit')).filter(function(r){return r.innerText.indexOf('locked')>=0})[0];"
            "return !!h&&h.querySelectorAll('.act').length===1&&(h.innerText.match(/locked/g)||[]).length===1})()", 3000) == 0);

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
        ok("the lock switch shows the password field", cdp_wait_bool(c,
            "document.getElementById('pp').hidden===false", 3000) == 0);
        ok("and its confirmation field", cdp_wait_bool(c,
            "document.getElementById('pp2').hidden===false", 3000) == 0);
        /* Show reveals BOTH fields (one state, as the app's eyes), Hide masks
         * them again, and the next sheet starts masked. */
        ok("each field offers Show", truthy(c,
            "document.querySelectorAll('#sheet .eye').length===2"
            "&&document.querySelector('#sheet .eye').textContent==='Show'"));
        cdp_eval_bool(c, "(function(){document.querySelector('#sheet .eye').click();return true})()", &(int){0});
        ok("Show reveals both passwords", cdp_wait_bool(c,
            "document.getElementById('pp').type==='text'&&document.getElementById('pp2').type==='text'"
            "&&document.querySelectorAll('#sheet .eye')[1].textContent==='Hide'", 3000) == 0);
        cdp_eval_bool(c, "(function(){document.querySelectorAll('#sheet .eye')[1].click();return true})()", &(int){0});
        ok("Hide masks them again", cdp_wait_bool(c,
            "document.getElementById('pp').type==='password'&&document.getElementById('pp2').type==='password'", 3000) == 0);
        cdp_eval_bool(c, "(function(){document.querySelector('#sheet .eye').click();openSheet();return true})()", &(int){0});
        ok("a new sheet starts masked", cdp_wait_bool(c,
            "document.getElementById('pp').type==='password'&&document.querySelector('#sheet .eye').textContent==='Show'", 3000) == 0);
        /* back to the state the refusals below expect: lock on, no password yet */
        cdp_eval_bool(c, "(function(){document.getElementById('v').value='safe-combo-1234';"
                         "document.getElementById('vk').value='locker';"
                         "var e=document.getElementById('enc');e.checked=true;e.dispatchEvent(new Event('change'));"
                         "window.__alert=null;document.getElementById('save').click();return true})()", &(int){0});
        ok("and says a forgotten password loses the note", cdp_wait_bool(c,
            "document.getElementById('ppnote').hidden===false"
            "&&document.getElementById('ppnote').textContent==='Forget this password and the note is gone for good.'",
            3000) == 0);
        ok("no password is refused, in those words", cdp_wait_bool(c,
            "window.__alert==='Enter a password to lock it.'", 3000) == 0);
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
        ok("mismatched passwords are refused, in those words", cdp_wait_bool(c,
            "window.__alert==='The passwords don\\'t match.'", 3000) == 0);
        ok("the mismatch keeps the sheet open", cdp_wait_bool(c,
            "document.getElementById('sheet').hidden===false", 3000) == 0);
        ok("and saved nothing",
           cdp_eval_bool(c, "(function(){window.__k=null;fetch('/api/get?keys=locker')"
                            ".then(function(r){return r.text()}).then(function(t){window.__k=t});return true})()",
                         &(int){0}) == 0 &&
           cdp_wait_bool(c, "window.__k!==null&&window.__k.indexOf('safe-combo-1234')<0"
                            "&&window.__k.indexOf('aisc:')<0", 5000) == 0);

        /* Over the inline cap (AIS_SECRET_INLINE_MAX, 6000 bytes) the page
         * refuses BEFORE sending, in the app's sentence, and keeps the sheet.
         * A 12 KB note once drew "check AIS is still running" from a server
         * that was up, and a plain save of the same text works. */
        cdp_eval_bool(c, "(function(){window.__alert=null;"
                         "document.getElementById('pp2').value='pw123';"
                         "document.getElementById('v').value=new Array(6002).join('s');"
                         "document.getElementById('save').click();return true})()", &(int){0});
        ok("a locked note over 6000 characters is refused, in those words", cdp_wait_bool(c,
            "window.__alert==='A locked note holds about 6000 letters, fewer in other "
            "alphabets. Split it, or save it without a password.'", 3000) == 0);
        ok("the over-long note keeps the sheet open", cdp_wait_bool(c,
            "document.getElementById('sheet').hidden===false", 3000) == 0);
        /* a tag over 255 BYTES is refused the same way (the engine's unit is the
         * byte: 100 Cyrillic letters are 200 bytes, 130 are 260) */
        cdp_eval_bool(c, "(function(){window.__alert=null;"
                         "document.getElementById('v').value='safe-combo-1234';"
                         "document.getElementById('vk').value='locker '+new Array(131).join('\u0436');"
                         "document.getElementById('save').click();return true})()", &(int){0});
        ok("a tag over 255 bytes is refused, in those words", cdp_wait_bool(c,
            "window.__alert==='One of your tags is too long (max 255 letters, fewer in other alphabets).'", 3000) == 0);
        ok("and saved nothing",
           cdp_eval_bool(c, "(function(){window.__k=null;fetch('/api/get?keys=locker')"
                            ".then(function(r){return r.text()}).then(function(t){window.__k=t});return true})()",
                         &(int){0}) == 0 &&
           cdp_wait_bool(c, "window.__k!==null&&window.__k.indexOf('aisc:')<0", 5000) == 0);

        /* A server refusal (413) reaches the user in the server's own sentence:
         * a plain note past the request buffer. The pre-checks let it through,
         * and the alert must not blame a dead server. */
        cdp_eval_bool(c, "(function(){window.__alert=null;"
                         "document.getElementById('vk').value='locker';"
                         "document.getElementById('enc').checked=false;"
                         "document.getElementById('v').value=new Array(70001).join('p');"
                         "document.getElementById('save').click();return true})()", &(int){0});
        ok("a plain note over 64 KB shows the server's own sentence", cdp_wait_bool(c,
            "window.__alert==='This note is too long to save here (over 64 KB). "
            "Split it into two notes.'", 5000) == 0);
        ok("and keeps the sheet open", cdp_wait_bool(c,
            "document.getElementById('sheet').hidden===false", 3000) == 0);

        cdp_eval_bool(c, "(function(){document.getElementById('v').value='safe-combo-1234';"
                         "document.getElementById('enc').checked=true;"
                         "document.getElementById('save').click();return true})()", &(int){0});
        ok("matching passwords save and close", cdp_wait_bool(c,
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
     * after the tag matches, under one "Found in the text" separator,
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
        "&&document.getElementById('out').innerText.indexOf('Found in the text')"
        "<document.getElementById('out').innerText.indexOf('plumber Mario')", 3000) == 0);
    ok("no tag matched, so no rows precede the separator", cdp_wait_bool(c,
        "document.getElementById('out').querySelectorAll('.hit').length===1", 3000) == 0);
    ok("the count line is the value half's", cdp_wait_bool(c,
        "document.getElementById('count').textContent.indexOf('1 result')>=0", 3000) == 0);
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
        "return t.indexOf('dorsoduro')>=0&&t.indexOf('Found in the text')>t.indexOf('dorsoduro')})()",
        5000) == 0);
    ok("the value-only record renders after it", cdp_wait_bool(c,
        "(function(){var t=document.getElementById('out').innerText;"
        "return t.indexOf('best gelato in rome')>t.indexOf('Found in the text')})()", 3000) == 0);
    ok("the both-ways record is not duplicated", cdp_wait_bool(c,
        "document.getElementById('out').innerText.split('dorsoduro').length===2", 3000) == 0);
    ok("the count names both halves", cdp_wait_bool(c,
        "document.getElementById('count').textContent"
        ".indexOf('1 result + 1 in the text')>=0", 3000) == 0);
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

    /* Import bookmarks: the Sync sheet's links button feeds a file to
     * /api/import-links; afterwards the page shows the batch under its marker
     * key. The file is built in the page (a File object) so no picker is
     * needed; alert() is stubbed, since a real one would block the protocol. */
    cdp_eval_bool(c, "(function(){openSync();return true})()", &(int){0});
    ok("the Sync sheet offers Import bookmarks", cdp_wait_bool(c,
        "(function(){var b=document.getElementById('linksbtn');"
        "return !!b&&b.textContent==='Import bookmarks'})()", 3000) == 0);
    ok("its file input takes csv and html", truthy(c,
        "(function(){var a=document.getElementById('filelinks').accept;"
        "return a.indexOf('.csv')>=0&&a.indexOf('.html')>=0})()"));
    cdp_eval_bool(c, "(function(){window.__msg=null;window.alert=function(m){window.__msg=m};"
        "var f=new File(['title,url,time_added,tags,status\\n"
        "Bread,https://bread.example/,1690000000,baking|rye,unread\\n'],'part_000000.csv');"
        "linksImport([f]);return true})()", &(int){0});
    ok("a Pocket csv imports and says so", cdp_wait_bool(c,
        "!!window.__msg&&window.__msg.indexOf('Imported 1 link under \\'pocket\\'')===0", 5000) == 0);
    ok("the page then shows the batch under its marker", cdp_wait_bool(c,
        "document.getElementById('q').value==='pocket'&&"
        "document.getElementById('out').innerText.indexOf('bread.example')>=0", 5000) == 0);
    ok("a file with no url column is refused by name", cdp_eval_bool(c,
        "(function(){window.__msg=null;var f=new File(['name,age\\nbob,3\\n'],'people.csv');"
        "linksImport([f]);return true})()", &(int){0}) == 0 &&
        cdp_wait_bool(c, "!!window.__msg&&window.__msg.indexOf('people.csv: no url column')>=0", 5000) == 0);

    /* ---- the Add sheet under a mouse ------------------------------------
     * Issue #3: on Windows, selecting text in a field closed the whole form.
     * A drag that starts in a field and ends on the dimmed backdrop makes the
     * browser fire the click on their common ancestor, the backdrop, and the
     * backdrop's click handler closed the sheet. The sheet may close only when
     * the press started on the backdrop too. Real mouse events, not .click(). */
    cdp_eval_bool(c, "(function(){document.getElementById('q').value='';setView('timeline');openSheet();return true})()", &(int){0});
    ok("the Add sheet is open", cdp_wait_bool(c, "document.getElementById('sheet').hidden===false", 3000) == 0);
    int vx = 0, vy = 0;
    ok("the text field has a place on screen",
       cdp_eval_int(c, "Math.round(document.getElementById('v').getBoundingClientRect().left+20)", &vx) == 0 &&
       cdp_eval_int(c, "Math.round(document.getElementById('v').getBoundingClientRect().top+10)", &vy) == 0);
    ok("the corner of the page is the backdrop", truthy(c,
        "document.elementFromPoint(4,4)===document.getElementById('sheet')"));
    cdp_mouse(c, "mousePressed", vx, vy);
    cdp_mouse(c, "mouseMoved", 4, 4);
    cdp_mouse(c, "mouseReleased", 4, 4);
    ok("a drag-select from a field past the card keeps the sheet",
       cdp_wait_bool(c, "document.getElementById('sheet').hidden===false", 500) == 0);
    /* and a sheet with typed text closes only by Cancel or Save: a stray
     * click on the backdrop, or an Escape, must not throw the draft away */
    cdp_eval_bool(c, "(function(){document.getElementById('v').value='a draft';return true})()", &(int){0});
    cdp_mouse(c, "mousePressed", 4, 4);
    cdp_mouse(c, "mouseReleased", 4, 4);
    ok("a backdrop click keeps a sheet with typed text",
       cdp_wait_bool(c, "document.getElementById('sheet').hidden===false", 500) == 0);
    cdp_key(c, "Escape");
    ok("so does Escape", cdp_wait_bool(c, "document.getElementById('sheet').hidden===false", 500) == 0
       && truthy(c, "document.getElementById('v').value==='a draft'"));
    cdp_eval_bool(c, "(function(){document.getElementById('v').value='';return true})()", &(int){0});
    cdp_mouse(c, "mousePressed", 4, 4);
    cdp_mouse(c, "mouseReleased", 4, 4);
    ok("a click on the backdrop closes an untouched sheet", cdp_wait_bool(c, "document.getElementById('sheet').hidden===true", 3000) == 0);
    cdp_eval_bool(c, "(function(){openSheet();return true})()", &(int){0});
    cdp_key(c, "Escape");
    ok("so does Escape", cdp_wait_bool(c, "document.getElementById('sheet').hidden===true", 3000) == 0);
    /* the Edit sheet follows the same rule */
    cdp_eval_bool(c, "(function(){openEdit(2,'https://example.org/canal-EDITED');return true})()", &(int){0});
    ok("the Edit sheet is open", cdp_wait_bool(c, "document.getElementById('editsheet').hidden===false&&edTags.length>0", 3000) == 0);
    cdp_eval_bool(c, "(function(){document.getElementById('edval').value+='x';return true})()", &(int){0});
    cdp_key(c, "Escape");
    ok("Escape keeps an edited record", cdp_wait_bool(c, "document.getElementById('editsheet').hidden===false", 500) == 0);
    cdp_eval_bool(c, "(function(){var e=document.getElementById('edval');e.value=e.value.slice(0,-1);return true})()", &(int){0});
    cdp_key(c, "Escape");
    ok("and closes an unedited one", cdp_wait_bool(c, "document.getElementById('editsheet').hidden===true", 3000) == 0);

    /* Issue #2: a term left in the search box landed in the new item's tags.
     * Tags are typed fresh far more often than a searched tag is reused, so
     * the Add sheet opens with empty tags from every view. */
    cdp_eval_bool(c, "(function(){document.getElementById('q').value='stale';setView('timeline');return true})()", &(int){0});
    cdp_wait_bool(c, HIT, 3000);
    cdp_eval_bool(c, "(function(){openSheet();return true})()", &(int){0});
    ok("Add from Recent leaves the tags empty", cdp_wait_bool(c,
        "document.getElementById('sheet').hidden===false&&document.getElementById('vk').value===''", 3000) == 0);
    ok("and lands on the text field", truthy(c, "document.activeElement===document.getElementById('v')"));
    cdp_eval_bool(c, "(function(){closeSheet();document.getElementById('q').value='venice';setView('recall');return true})()", &(int){0});
    cdp_wait_bool(c, HIT, 3000);
    cdp_eval_bool(c, "(function(){openSheet();return true})()", &(int){0});
    ok("Add from a search leaves the tags empty too", cdp_wait_bool(c,
        "document.getElementById('sheet').hidden===false&&document.getElementById('vk').value===''", 3000) == 0);
    cdp_eval_bool(c, "(function(){closeSheet();document.getElementById('q').value='';return true})()", &(int){0});

    cdp_close(c);
    printf("cdp: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
