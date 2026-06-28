/* serve.c -- `ais serve`: an OPTIONAL built-in web GUI. See serve.h.
 *
 * ====================================================================
 *  NOTE TO READERS: this file is a GUI WRAPPER, not the program.
 *  The actual AIS -- the index, the store, the algorithms -- lives in
 *  ais.c, store.c, merge.c, post.c, compact.c, in the project's normal
 *  C style. This file only lets a browser drive that engine, and it
 *  embeds a small web page as a C string (PAGE, below) so the binary
 *  stays self-contained. That blob is HTML/JS, NOT C: please don't read
 *  it as a sample of how AIS is written, and you rarely need to touch
 *  it. It is the only GUI file under c/; everything else here is engine.
 * ====================================================================
 *
 * How it works: a tiny single-threaded HTTP/1.0 loop on 127.0.0.1 serving the
 * page plus two endpoints that call the engine directly. No Python, no
 * framework, no DB -- the binary is the backend (the whole servlet/DB/auth
 * stack kul needs is unnecessary here). A SKETCH: localhost only, one client at
 * a time, the request must fit a single read.
 */
#define _DEFAULT_SOURCE          /* htonl, strtok_r */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "ais.h"
#include "common.h"
#include "doc.h"
#include "secret.h"       /* GUI encrypt: secret_encrypt for the "aisc:" marker */
#include "locate.h"       /* ais_default_set: persist the chosen store */
#include "win.h"          /* Winsock + socket shims on native Windows; empty on POSIX */
#include "serve.h"

/* socket I/O is read()/write()/close() on POSIX, recv()/send()/closesocket()
 * on native Windows (where a SOCKET is not a file descriptor). */
#ifdef _WIN32
#define SOCK_READ(fd, b, n)  recv((SOCKET)(fd), (b), (int)(n), 0)
#define SOCK_WRITE(fd, b, n) send((SOCKET)(fd), (b), (int)(n), 0)
#define SOCK_CLOSE(fd)       closesocket((SOCKET)(fd))
#else
#define SOCK_READ(fd, b, n)  read((fd), (b), (n))
#define SOCK_WRITE(fd, b, n) write((fd), (b), (n))
#define SOCK_CLOSE(fd)       close((fd))
#endif

/* ---- the GUI page (HTML + JavaScript, NOT C) ----------------------------
 * This is the web wrapper's user interface, embedded as a string so the binary
 * is self-contained. The cramped one-string-literal-per-line shape is simply
 * how you paste a web page into a C source file -- it is NOT the project's C
 * style, and it is not part of the engine. To change the UI, edit the PAGE
 * string below directly -- it is the only copy. Vanilla JS; the API is
 * form-encoded keys + a plain-text body/reply (no JSON, and a text/plain POST
 * is a "simple" request, so no CORS preflight). */
static const char PAGE[] =
"<!doctype html><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<meta name=theme-color content=#1a0dab><title>AIS</title>"
"<style>"
":root{--accent:#1a0dab;--line:#e3e3ea;--muted:#6b6b75}*{box-sizing:border-box}"
"body{font:16px/1.45 system-ui,sans-serif;color:#14141a;background:#efeff6;margin:0}"
"#bar{position:sticky;top:0;z-index:5;padding:12px 16px;background:rgba(255,255,255,.62);"
"backdrop-filter:blur(14px);-webkit-backdrop-filter:blur(14px);border-bottom:1px solid var(--line)}"
".titlerow{display:flex;align-items:baseline;gap:.5rem}"
".brand{font-size:1.35rem;font-weight:700}.muted{color:var(--muted)}"
"#count{margin-left:auto;font-size:.8rem}"
".searchrow{display:flex;align-items:center;margin-top:.6rem;background:#fff;"
"border:1px solid var(--line);border-radius:28px;padding:0 .8rem}"
"#q{flex:1;font:inherit;border:0;outline:none;background:transparent;padding:.7rem .4rem}"
"#out{max-width:720px;margin:0 auto;padding:.5rem 1rem 6rem}"
".hit{padding:.85rem .2rem;border-bottom:1px solid var(--line);word-break:break-word}"
".hit:last-child{border-bottom:0}.hit a{color:var(--accent);text-decoration:none}"
".empty{color:var(--muted);text-align:center;margin-top:3rem}"
".storerow{font-size:.72rem;margin-top:.45rem;display:flex;gap:.4rem;align-items:center}"
".link{border:0;background:none;color:var(--accent);cursor:pointer;font:inherit;text-decoration:underline;padding:0}"
".fab{position:fixed;right:18px;bottom:18px;border:0;border-radius:30px;padding:.9rem 1.3rem;"
"cursor:pointer;font:inherit;font-weight:600;color:#fff;background:var(--accent);box-shadow:0 4px 14px rgba(0,0,0,.25)}"
"#sheet{position:fixed;inset:0;z-index:10;background:rgba(0,0,0,.35);display:flex;align-items:center;justify-content:center}"
"#sheet[hidden]{display:none}"
".card{width:100%;max-width:560px;background:#fff;border-radius:18px;padding:1.2rem;margin:1rem}"
".card h2{margin:0 0 1rem;font-size:1.15rem}"
".card textarea,.card input{width:100%;font:inherit;padding:.7rem .8rem;border:1px solid var(--line);"
"border-radius:10px;margin-bottom:.8rem;background:#fafafc}"
".actions{display:flex;justify-content:flex-end;gap:.6rem}"
".actions button{font:inherit;padding:.6rem 1.1rem;border-radius:10px;cursor:pointer}"
".ghost{border:1px solid var(--line);background:#fff}"
".primary{border:0;background:var(--accent);color:#fff;font-weight:600}"
".seg{display:flex;gap:.3rem;margin-top:.5rem}"
".segbtn{flex:1;border:1px solid var(--line);background:#fff;color:var(--muted);font:inherit;"
"font-size:.85rem;padding:.4rem;border-radius:8px;cursor:pointer}"
".segbtn.active{background:var(--accent);color:#fff;border-color:var(--accent)}"
".daygroup{font-size:.74rem;color:var(--muted);text-transform:uppercase;letter-spacing:.04em;"
"margin:1.1rem 0 .2rem;padding-bottom:.2rem;border-bottom:1px solid var(--line)}"
".meta{font-size:.74rem;color:var(--muted);margin-top:.2rem}"
".tagrow{display:flex;align-items:center;justify-content:space-between;padding:.55rem .2rem;border-bottom:1px solid var(--line)}"
".taglink{border:0;background:none;color:var(--accent);font:inherit;cursor:pointer;padding:0;text-align:left;word-break:break-word}"
".tagcount{font-size:.8rem;color:var(--muted);background:#fff;border:1px solid var(--line);border-radius:10px;padding:.1rem .5rem;min-width:2rem;text-align:center}"
".act{display:flex;gap:.8rem;margin-top:.35rem}"
".actbtn{border:0;background:none;color:var(--muted);font:inherit;font-size:.8rem;cursor:pointer;padding:0}"
".actbtn:hover{color:var(--accent)}"
".actbtn.del:hover{color:#c0392b}"
".loadmore{display:block;width:100%;margin:1rem 0;padding:.6rem;border:1px solid var(--line);background:#fff;color:var(--accent);border-radius:8px;cursor:pointer;font:inherit}"
".loadmore:hover{background:#f5f5fa}"
"</style>"
"<header id=bar><div class=titlerow><span class=brand>AIS</span><span id=count class=muted></span></div>"
"<div class=searchrow><input id=q type=search placeholder='type keys to get...' autocomplete=off autofocus>"
"<label class=allk style='display:flex;align-items:center;gap:.4rem;font-size:.85rem;color:var(--muted);margin-top:.45rem'><input id=anyk type=checkbox style='width:auto'> Match any key</label></div>"
"<div class=seg><button id=seg-recall class='segbtn active'>Get</button>"
"<button id=seg-timeline class=segbtn>Timeline</button>"
"<button id=seg-tags class=segbtn>Tags</button></div>"
"<div id=tlrange style='display:none;gap:.4rem;align-items:center;margin-top:.5rem;font-size:.85rem;color:var(--muted)'>"
"<span>from</span><input id=tlfrom type=date style='font:inherit'>"
"<span>to</span><input id=tlto type=date style='font:inherit'>"
"<button id=tlclear class=link>clear</button></div>"
"<div class=storerow><span id=store class=muted></span><button id=storebtn class=link>change</button></div></header>"
"<main id=out><p class=empty>Type keys, then Enter. Click + Add to save.</p></main>"
"<button id=addbtn class=fab>+ Add</button>"
"<div id=sheet hidden><div class=card><h2>Add to your memory</h2>"
"<input id=vk placeholder='Keys (space-separated, optional)'>"
"<textarea id=v rows=3 placeholder='What to remember: a link, a note, a number...'></textarea>"
"<div class=encrow style='display:flex;align-items:center;gap:.5rem;margin:.1rem 0'>"
"<label style='display:flex;align-items:center;gap:.35rem;font-size:.85rem;color:var(--muted);white-space:nowrap'>"
"<input id=enc type=checkbox style='width:auto'> Encrypt</label>"
"<input id=pp type=password placeholder='Passphrase' hidden style='flex:1'></div>"
"<div class=actions><button id=cancel class=ghost>Cancel</button><button id=save class=primary>Save</button></div>"
"</div></div>"
"<script>"
"var $=function(i){return document.getElementById(i)};"
"var view='recall';"
"var tlBefore=0,tlDay=null,tlN=0,tlPage=100;"   /* timeline keyset paging state */
/* fillVal: append V to NODE, turning every embedded http(s) URL into a real
 * link -- not only values that are wholly a URL (a "Title - https://..." value
 * gets its URL linked, with the title left as text). */
"function fillVal(node,v){var re=/https?:\\/\\/[^\\s]+/g,last=0,m;"
"while((m=re.exec(v))!==null){"
"if(m.index>last)node.appendChild(document.createTextNode(v.slice(last,m.index)));"
"var a=document.createElement('a');a.href=m[0];a.textContent=m[0];"
"a.target='_blank';a.rel='noopener';node.appendChild(a);last=m.index+m[0].length}"
"if(last<v.length)node.appendChild(document.createTextNode(v.slice(last)))}"
/* An "aisc:" value is shown as an opaque lock + a Reveal button; revealing
 * prompts for the passphrase and decrypts via /api/reveal (passphrase in the
 * POST body, never the URL). The cleartext is shown until the next render. */
"function fillSecret(node,v){var s=document.createElement('span');s.textContent='\\uD83D\\uDD12 encrypted ';s.style.color='var(--muted)';"
"var b=document.createElement('button');b.className='actbtn';b.textContent='Reveal';"
"b.onclick=function(){revealSecret(node,v)};node.appendChild(s);node.appendChild(b)}"
"async function revealSecret(node,v){var pp=prompt('Passphrase:');if(!pp)return;"
"var r=await fetch('/api/reveal',{method:'POST',body:pp+'\\n'+v});var t=await r.text();"
"node.innerHTML='';if(t){fillVal(node,t)}else{node.textContent='(cannot decrypt)'}}"
"function render(t,q,ms){var L=t.split('\\n').filter(function(s){return s.length});"
"$('count').textContent=L.length+' result'+(L.length==1?'':'s')+' - '+ms.toFixed(0)+' ms';"
"var o=$('out');o.innerHTML='';"
"if(!L.length){o.textContent='No results for '+q;o.className='empty';return}o.className='';"
"L.forEach(function(ln){var p=ln.indexOf('|'),id=p>=0?ln.slice(0,p):'',v=p>=0?ln.slice(p+1):ln,"
"r=document.createElement('div');r.className='hit';"
"(v.indexOf('aisc:')==0?fillSecret(r,v):fillVal(r,v));if(id)r.appendChild(rowActions(id));o.appendChild(r)})}"
/* per-row edit (attach/detach keys by id) and delete; both refresh the view */
"function rowActions(id){var d=document.createElement('div');d.className='act';"
"var e=document.createElement('button');e.className='actbtn';e.textContent='edit keys';"
"e.onclick=function(){editRec(id)};"
"var x=document.createElement('button');x.className='actbtn del';x.textContent='\\u2715 delete';"
"x.onclick=function(){delRec(id)};"
"d.appendChild(e);d.appendChild(x);return d}"
"async function delRec(id){if(!confirm('Delete this record?'))return;"
"await fetch('/api/del?id='+id,{method:'POST'});setView(view)}"
"async function editRec(id){var s=prompt('Edit keys: a KEY adds it, -KEY removes it (space-separated)');"
"if(s===null||!s.trim())return;"
"await fetch('/api/update?id='+id+'&keys='+encodeURIComponent(s.trim()),{method:'POST'});setView(view)}"
"async function recall(){var q=$('q').value.trim();if(!q)return;var t0=performance.now();"
"var oo=$('anyk')&&$('anyk').checked?'&or=1':'';"
"var t=await(await fetch('/api/get?keys='+encodeURIComponent(q)+oo)).text();render(t,q,performance.now()-t0)}"
"function parseTL(ln){var a=ln.indexOf('|'),b=ln.indexOf('|',a+1),c=ln.indexOf('|',b+1);"
"return{id:ln.slice(0,a),ts:ln.slice(a+1,b),keys:ln.slice(b+1,c),value:ln.slice(c+1)}}"
"function fmtDay(d){var p=d.split('-'),M=['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];"
"return p[2]+' '+M[(+p[1])-1]+' '+p[0]}"
/* a stored ts (UTC '...Z', or an old local '...' with no zone) -> LOCAL day+time
 * for display. Engine stores UTC; the viewer localizes. */
"function locDT(ts){if(!ts)return null;if(ts.length<=10)return{day:ts,time:''};"
"var d=new Date(ts);if(isNaN(d.getTime()))return null;"
"var p=function(n){return(n<10?'0':'')+n};"
"return{day:d.getFullYear()+'-'+p(d.getMonth()+1)+'-'+p(d.getDate()),time:p(d.getHours())+':'+p(d.getMinutes())}}"
/* keyset paging: each call fetches `count` records older than the last id shown
 * (tlBefore); 'more' appends, otherwise it reloads from the newest. */
"async function loadTimeline(more){var o=$('out');"
"var f=$('tlfrom')?$('tlfrom').value:'',tt=$('tlto')?$('tlto').value:'';"
"if(!more){tlBefore=0;tlDay=null;tlN=0;o.className='';o.innerHTML=''}"
"var u='/api/timeline?count='+tlPage+(tlBefore>0?'&before='+tlBefore:'')+(f?'&from='+f:'')+(tt?'&to='+tt:'');"
"var L=(await(await fetch(u)).text()).split('\\n').filter(function(s){return s.length});"
"var mb=$('tlmore');if(mb)mb.remove();"
"if(!tlN&&!L.length){o.innerHTML='<p class=empty>Nothing saved yet.</p>';return}"
"L.forEach(function(ln){var r=parseTL(ln),lt=locDT(r.ts),d=lt?lt.day:'';"
"if(d!==tlDay){tlDay=d;var h=document.createElement('div');h.className='daygroup';"
"h.textContent=d?fmtDay(d):'(undated)';o.appendChild(h)}"
"var row=document.createElement('div');row.className='hit';fillVal(row,r.value);"
"var m=document.createElement('div');m.className='meta';"
"var tm=lt&&lt.time?lt.time+' - ':'';"
"m.textContent=tm+(r.keys||'(no keys)');row.appendChild(m);"
"if(r.id){row.appendChild(rowActions(r.id));tlBefore=r.id}o.appendChild(row)});"
"tlN+=L.length;$('count').textContent=tlN+' record'+(tlN==1?'':'s');"
"if(L.length==tlPage){var b=document.createElement('button');b.id='tlmore';b.className='loadmore';"
"b.textContent='Load more';b.onclick=function(){loadTimeline(true)};o.appendChild(b)}}"
"async function loadTags(){var t=await(await fetch('/api/tags')).text();"
"var L=t.split('\\n').filter(function(s){return s.length}),o=$('out');o.className='';o.innerHTML='';"
"$('count').textContent=L.length+' tag'+(L.length==1?'':'s');"
"if(!L.length){o.innerHTML='<p class=empty>No tags yet.</p>';return}"
"L.forEach(function(ln){var p=ln.indexOf('|'),c=ln.slice(0,p),k=ln.slice(p+1);"
"var row=document.createElement('div');row.className='tagrow';"
"var b=document.createElement('button');b.className='taglink';b.textContent=k;"
"b.onclick=function(){$('q').value=k;setView('recall')};"
"var n=document.createElement('span');n.className='tagcount';n.textContent=c;"
"row.appendChild(b);row.appendChild(n);o.appendChild(row)})}"
"function setView(v){view=v;['recall','timeline','tags'].forEach(function(k){"
"$('seg-'+k).className='segbtn'+(k==v?' active':'')});"
"$('tlrange').style.display=(v=='timeline')?'flex':'none';"   /* date range only in Timeline */
"if(v=='recall'){var q=$('q').value.trim();if(q)recall();"
"else{$('out').innerHTML='<p class=empty>Type keys, then Enter.</p>';$('out').className='';$('count').textContent=''}}"
"else if(v=='timeline')loadTimeline();else loadTags()}"
"function openSheet(){$('vk').value=$('q').value.trim();$('enc').checked=false;$('pp').value='';$('pp').hidden=true;$('sheet').hidden=false;$('v').focus()}"
"function closeSheet(){$('sheet').hidden=true;$('v').value='';$('pp').value=''}"
"async function save(){var v=$('v').value.trim();if(!v)return;var k=$('vk').value.trim();"
"var enc=$('enc').checked,pp=$('pp').value;"
"if(enc&&!pp){alert('Enter a passphrase to encrypt');return}"
"await fetch('/api/put?keys='+encodeURIComponent(k)+(enc?'&enc=1':''),{method:'POST',body:enc?(pp+'\\n'+v):v});"
"closeSheet();$('q').value=k;recall()}"
"$('q').addEventListener('keydown',function(e){if(e.key=='Enter')setView('recall')});"
"$('seg-recall').onclick=function(){setView('recall')};"
"$('seg-timeline').onclick=function(){setView('timeline')};"
"$('seg-tags').onclick=function(){setView('tags')};"
"$('tlfrom').onchange=function(){loadTimeline()};$('tlto').onchange=function(){loadTimeline()};"
"$('tlclear').onclick=function(){$('tlfrom').value='';$('tlto').value='';loadTimeline()};"
"$('addbtn').onclick=openSheet;$('cancel').onclick=closeSheet;$('save').onclick=save;"
"$('enc').onchange=function(){$('pp').hidden=!$('enc').checked;if($('enc').checked)$('pp').focus()};"
"$('sheet').addEventListener('click',function(e){if(e.target==$('sheet'))closeSheet()});"
"async function loadStore(){$('store').textContent='store: '+await(await fetch('/api/where')).text()}"
"async function changeStore(){var cur=await(await fetch('/api/where')).text();"
"var d=prompt('Index folder (full path):',cur);if(!d||d==cur)return;"
"var r=await fetch('/api/store',{method:'POST',body:d});"
"if(r.ok){$('q').value='';$('out').innerHTML='';$('count').textContent='';loadStore()}"
"else{alert('Could not open that index')}}"
"$('storebtn').onclick=changeStore;loadStore();"
"</script>";

static void write_all(int fd, const char *p, size_t n)
{
    while (n > 0) {
        ssize_t w = SOCK_WRITE(fd, p, n);
        if (w <= 0)
            return;
        p += w;
        n -= (size_t)w;
    }
}

static void send_head(int fd, const char *ctype)
{
    char h[128];
    int n = snprintf(h, sizeof(h),
                     "HTTP/1.0 200 OK\r\nContent-Type: %s\r\nConnection: close\r\n\r\n",
                     ctype);
    if (n > 0)
        write_all(fd, h, (size_t)n);
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* in-place URL-decode (%xx and '+'). */
static void url_decode(char *s)
{
    char *o = s;
    while (*s != '\0') {
        int hi, lo;
        if (*s == '+') {
            *o++ = ' '; s++;
        } else if (*s == '%' && (hi = hexval((unsigned char)s[1])) >= 0
                             && (lo = hexval((unsigned char)s[2])) >= 0) {
            *o++ = (char)(hi * 16 + lo); s += 3;
        } else {
            *o++ = *s++;
        }
    }
    *o = '\0';
}

/* ---- get: stream each matching record's values to the socket ------------ */
struct sink { ais *a; int fd; };

static int on_value(long id, const char *value, void *vp)
{
    struct sink *s = vp;
    char line[AIS_LINE_MAX];
    int n = snprintf(line, sizeof(line), "%ld|%s\n", id, value);
    if (n > 0)
        write_all(s->fd, line, (size_t)n);
    return 0;
}

static int on_id(long id, void *vp)
{
    struct sink *s = vp;
    ais_record(s->a, id, on_value, s);
    return 0;
}

/* Get records under the keys: AND (intersection) by default, OR (union) when
 * want_or is set (the "Match any key" box). The user toggles it -- if AND finds
 * nothing they check the box and Get again. No automatic relaxation. */
static void do_get(ais *a, char *keys, int want_or, int fd)
{
    char *kv[AIS_KEYS_MAX];
    int nkeys = 0;
    char *tok, *save;
    struct sink s;

    for (tok = strtok_r(keys, " ", &save); tok != NULL && nkeys < AIS_KEYS_MAX;
         tok = strtok_r(NULL, " ", &save))
        kv[nkeys++] = tok;

    s.a = a; s.fd = fd;
    if (nkeys > 0)
        ais_get(a, kv, nkeys, want_or ? AIS_OR : AIS_AND, on_id, &s);
}

/* ---- put: the WHOLE body is one record ----------------------------------
 * A pasted block is one entry, not one record per line: ais_put_value() keeps
 * a single line as a plain record and routes a multi-line value to a blob.
 * Returns 1 if stored, 0 if empty/failed. */
static long do_put(ais *a, const char *keys, char *body)
{
    return ais_put_value(a, keys, body) >= 0 ? 1 : 0;
}

/* Encrypt save (?enc=1): BODY is "passphrase\nvalue..." -- the passphrase rides
 * the POST body (never the URL, so it stays out of the browser history) on this
 * localhost-only server. Encrypts VALUE under the passphrase and puts the
 * "aisc:" marker. 1 if stored, 0 on malformed/failure/crypto-not-built. */
static long do_put_enc(ais *a, const char *keys, char *body)
{
    char marked[8192];
    char *value;
    long mn, rc = 0;

    value = strchr(body, '\n');
    if (value == NULL)
        return 0;                          /* expected "passphrase\nvalue" */
    *value++ = '\0';                        /* body -> passphrase, value -> the rest */
    mn = secret_encrypt((const unsigned char *)value, strlen(value),
                        (const unsigned char *)body, strlen(body), marked, sizeof marked);
    secret_wipe(body, strlen(body));        /* wipe the passphrase from the request buffer */
    if (mn >= 0)
        rc = (ais_put(a, keys, marked) >= 0) ? 1 : 0;
    secret_wipe(marked, sizeof marked);
    return rc;
}

/* ---- timeline: "id|ts|keys|value" newest-first (dateless first) ---------- */
static int tl_sink(long id, const char *ts, const char *keys,
                   const char *value, void *vp)
{
    struct sink *s = vp;
    char line[AIS_LINE_MAX];
    int n = snprintf(line, sizeof(line), "%ld|%s|%s|%s\n", id, ts, keys, value);
    if (n > 0)
        write_all(s->fd, line, (size_t)n);
    return 0;
}

/* ---- tags: "count|key", busiest first ----------------------------------- */
static int tag_sink(const char *key, long count, void *vp)
{
    struct sink *s = vp;
    char line[AIS_KEY_MAX + 32];
    int n = snprintf(line, sizeof(line), "%ld|%s\n", count, key);
    if (n > 0)
        write_all(s->fd, line, (size_t)n);
    return 0;
}

static void not_found(int fd)
{
    static const char nf[] =
        "HTTP/1.0 404 Not Found\r\nConnection: close\r\n\r\nnot found\n";
    write_all(fd, nf, sizeof(nf) - 1);
}

static const char *ctype_of(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot != NULL) {
        if (strcmp(dot, ".html") == 0) return "text/html";
        if (strcmp(dot, ".css")  == 0) return "text/css";
        if (strcmp(dot, ".js")   == 0) return "text/javascript";
        if (strcmp(dot, ".svg")  == 0) return "image/svg+xml";
        if (strcmp(dot, ".png")  == 0) return "image/png";
        if (strcmp(dot, ".json") == 0) return "application/json";
        if (strcmp(dot, ".webmanifest") == 0) return "application/manifest+json";
    }
    return "text/plain";
}

/* Serve an external asset <webdir>/<name> if present, so the look can be edited
 * as plain files (kul-style) instead of the embedded PAGE. webdir = $AIS_WEB,
 * else "gui/web". NAME must be one safe filename (letters/digits/._-), with no
 * '/' or "..", so the browser cannot escape the dir. Returns 1 if served. */
static int serve_asset(int fd, const char *name)
{
    const char *webdir = "gui/web";       /* external assets if present (dev); no env */
    char path[AIS_PATH_MAX], buf[8192];
    FILE *fp;
    size_t n;
    const char *p;

    if (name[0] == '\0')
        return 0;
    for (p = name; *p != '\0'; p++)
        if (!isalnum((unsigned char)*p) && *p != '.' && *p != '_' && *p != '-')
            return 0;                     /* reject '/', '..', anything unsafe */
    if (snprintf(path, sizeof(path), "%s/%s", webdir, name) >= (int)sizeof(path))
        return 0;
    fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;
    send_head(fd, ctype_of(name));
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        write_all(fd, buf, n);
    fclose(fp);
    return 1;
}

/* ---- one request -------------------------------------------------------- */
static void handle(ais *a, int fd)
{
    char buf[AIS_LINE_MAX];
    char nokeys[1] = "";
    ssize_t n;
    char *method, *path, *query, *body, *keys = nokeys, *sp;
    int want_or = 0;                      /* "Match any key" -> AIS_OR; default AND+relax */
    int enc = 0;                          /* ?enc=1 -> encrypt the value before storing */
    long reqid = 0;                       /* ?id= for /api/del and /api/update      */
    long before = 0;                      /* ?before= cursor for /api/timeline paging */
    int  count = 0;                       /* ?count= page size (0 => engine default)  */
    char *tlfrom = nokeys, *tlto = nokeys; /* ?from= ?to= date range (YYYY-MM-DD)      */

    n = SOCK_READ(fd, buf, sizeof(buf) - 1);   /* assume the request fits (a sketch) */
    if (n <= 0)
        return;
    buf[n] = '\0';

    body = strstr(buf, "\r\n\r\n");       /* split headers from body first... */
    if (body != NULL) { *body = '\0'; body += 4; }
    else              { body = buf + n; }

    method = buf;                         /* ...then parse the request line   */
    path = strchr(buf, ' ');
    if (path == NULL)
        return;
    *path++ = '\0';
    sp = strchr(path, ' ');
    if (sp != NULL) *sp = '\0';
    query = strchr(path, '?');
    if (query != NULL) *query++ = '\0';

    while (query != NULL && *query != '\0') {   /* params: keys=... & optional all=1 */
        char *amp = strchr(query, '&');
        if (amp != NULL) *amp = '\0';
        if (strncmp(query, "keys=", 5) == 0) {
            keys = query + 5;
            url_decode(keys);
        } else if (strncmp(query, "or=", 3) == 0) {
            want_or = (query[3] == '1');
        } else if (strncmp(query, "enc=", 4) == 0) {
            enc = (query[4] == '1');
        } else if (strncmp(query, "id=", 3) == 0) {
            reqid = atol(query + 3);
        } else if (strncmp(query, "before=", 7) == 0) {
            before = atol(query + 7);
        } else if (strncmp(query, "count=", 6) == 0) {
            count = atoi(query + 6);
        } else if (strncmp(query, "from=", 5) == 0) {
            tlfrom = query + 5;
            url_decode(tlfrom);
        } else if (strncmp(query, "to=", 3) == 0) {
            tlto = query + 3;
            url_decode(tlto);
        }
        query = (amp != NULL) ? amp + 1 : NULL;
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/get") == 0) {
        send_head(fd, "text/plain");
        do_get(a, keys, want_or, fd);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/put") == 0) {
        char msg[64];
        long c = enc ? do_put_enc(a, keys, body) : do_put(a, keys, body);
        int m = snprintf(msg, sizeof(msg), "saved %ld record(s)\n", c);
        send_head(fd, "text/plain");
        if (m > 0)
            write_all(fd, msg, (size_t)m);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/reveal") == 0) {
        /* body = "passphrase\nmarked-value"; decrypt and return the cleartext
         * (empty body = could not decrypt). The passphrase rides the body, not
         * the URL, so it stays out of the browser history. */
        char *value = strchr(body, '\n');
        send_head(fd, "text/plain");
        if (value != NULL) {
            unsigned char out[AIS_LINE_MAX];
            long n;
            *value++ = '\0';                  /* body -> passphrase */
            n = secret_decrypt(value, (const unsigned char *)body, strlen(body),
                               out, sizeof out);
            secret_wipe(body, strlen(body));
            if (n > 0)
                write_all(fd, (char *)out, (size_t)n);
            secret_wipe(out, sizeof out);
        }
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/del") == 0) {
        /* delete record ?id=N (the handle from the id|value lines) */
        if (reqid > 0 && ais_del(a, reqid) == 0) {
            send_head(fd, "text/plain");
            write_all(fd, "deleted\n", 8);
        } else {
            static const char e[] = "HTTP/1.0 400 Bad Request\r\n"
                "Connection: close\r\n\r\ncannot delete\n";
            write_all(fd, e, sizeof(e) - 1);
        }
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/update") == 0) {
        /* edit record ?id=N keys=...  (KEY attaches, -KEY detaches) */
        if (reqid > 0 && keys[0] != '\0' && ais_update(a, reqid, keys) == 0) {
            send_head(fd, "text/plain");
            write_all(fd, "updated\n", 8);
        } else {
            static const char e[] = "HTTP/1.0 400 Bad Request\r\n"
                "Connection: close\r\n\r\ncannot update\n";
            write_all(fd, e, sizeof(e) - 1);
        }
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/timeline") == 0) {
        struct sink s;
        s.a = a; s.fd = fd;
        send_head(fd, "text/plain");
        ais_timeline(a, before, count, tlfrom, tlto, tl_sink, &s);  /* keyset page + date range */
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/tags") == 0) {
        struct sink s;
        s.a = a; s.fd = fd;
        send_head(fd, "text/plain");
        ais_tags(a, tag_sink, &s);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/where") == 0) {
        send_head(fd, "text/plain");          /* the current store (index dir) */
        write_all(fd, a->dir, strlen(a->dir));
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/store") == 0) {
        /* switch the active index: the body is the new directory. Reopen it,
         * restoring the old one if it cannot be opened (single-threaded, so the
         * in-place reopen is safe). localhost only, single user. */
        char olddir[AIS_PATH_MAX];
        char *nd = body;
        size_t bl;
        while (*nd == ' ' || *nd == '\t') nd++;
        bl = strlen(nd);
        while (bl > 0 && (nd[bl-1] == '\r' || nd[bl-1] == '\n' ||
                          nd[bl-1] == ' '  || nd[bl-1] == '\t'))
            nd[--bl] = '\0';
        snprintf(olddir, sizeof(olddir), "%s", a->dir);
        if (nd[0] != '\0' && strcmp(nd, olddir) != 0) {
            ais_close(a);
            if (ais_open(a, nd) != 0) {
                static const char e[] = "HTTP/1.0 400 Bad Request\r\n"
                    "Connection: close\r\n\r\ncannot open that index\n";
                ais_open(a, olddir);          /* restore the previous store */
                write_all(fd, e, sizeof(e) - 1);
                return;                        /* accept loop closes fd */
            }
            ais_default_set(nd);              /* persist: it's the default next run */
        }
        send_head(fd, "text/plain");
        write_all(fd, a->dir, strlen(a->dir));
    } else if (strcmp(method, "GET") == 0) {
        /* an external asset (e.g. /style.css) if gui/web has it; the root falls
         * back to the embedded page so the binary still works with no files. */
        const char *name = (strcmp(path, "/") == 0) ? "index.html" : path + 1;
        if (serve_asset(fd, name)) {
            /* served from disk */
        } else if (strcmp(name, "index.html") == 0) {
            send_head(fd, "text/html");
            write_all(fd, PAGE, sizeof(PAGE) - 1);
        } else {
            not_found(fd);
        }
    } else {
        not_found(fd);
    }
}

int ais_serve(ais *a, int port)
{
    int sfd, cfd, yes = 1;
    struct sockaddr_in addr;

#ifdef _WIN32
    ais_net_init();             /* WSAStartup before any socket call */
#else
    signal(SIGPIPE, SIG_IGN);   /* a client hangup must not kill the server */
#endif

    sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0)
        return -1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   /* 127.0.0.1 only */
    addr.sin_port = htons((unsigned short)port);
    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        SOCK_CLOSE(sfd);
        return -1;
    }
    if (listen(sfd, 16) != 0) {
        SOCK_CLOSE(sfd);
        return -1;
    }
    fprintf(stderr, "ais serve: http://127.0.0.1:%d/  (Ctrl-C to stop)\n", port);

    /* best-effort: open the page in the user's browser (this is GUI-wrapper
     * behaviour). macOS `open`, Linux `xdg-open`; ignored if neither exists.
     * Suppressed when AIS_NO_OPEN is set -- for agents, screenshots, scripts and
     * CI that drive the server headlessly and must not spawn a browser window. */
    if (getenv("AIS_NO_OPEN") == NULL) {
        char cmd[224];
        int rc;
#ifdef _WIN32
        snprintf(cmd, sizeof(cmd), "start \"\" http://127.0.0.1:%d/", port);
#else
        snprintf(cmd, sizeof(cmd),
                 "{ xdg-open 'http://127.0.0.1:%d/' || open 'http://127.0.0.1:%d/'; }"
                 " >/dev/null 2>&1 &",
                 port, port);           /* xdg-open: Linux, open: macOS */
#endif
        rc = system(cmd);
        (void)rc;
    }

    for (;;) {
        cfd = accept(sfd, NULL, NULL);
        if (cfd < 0)
            continue;
        handle(a, cfd);
        SOCK_CLOSE(cfd);
    }
    /* not reached */
}
