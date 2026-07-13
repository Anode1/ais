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
#include "b64.h"           /* base64 a document blob's content onto the line-based wire */
#include "common.h"
#include "doc.h"
#include "secret.h"       /* GUI encrypt: secret_encrypt for the "aisc:" marker */
#include "locate.h"       /* ais_default_set: persist the chosen store */
#include "win.h"          /* Winsock + socket shims on native Windows; empty on POSIX */
#include "serve.h"

/* LAN sync (the GUI's Host/Join, mirroring the mobile Sync feature): available
 * only where the sync transport is -- POSIX plus the vendored crypto module, the
 * same guard sync.c uses. Elsewhere the routes report the build lacks it. */
#if !defined(_WIN32) && defined(__has_include) && __has_include("crypto/monocypher.h")
#  define SERVE_HAVE_SYNC 1
#  include <arpa/inet.h>
#  include <sys/wait.h>
#  include "crypto/ais_crypto.h"   /* aisc_token */
#  include "sync.h"                /* sync_serve, sync_pull, sync_parse_url, AIS_SYNC_PORT */
#endif

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
":root{--accent:#1a0dab;--line:#e3e3ea;--muted:#54545e;--bg:#efeff6;--fg:#14141a;--card:#fff;--field:#fafafc;--barbg:rgba(255,255,255,.62)}"
"@media(prefers-color-scheme:dark){:root{--accent:#9db4ff;--line:#33333e;--muted:#9b9ba7;--bg:#15151b;--fg:#e7e7ef;--card:#23232c;--field:#2b2b35;--barbg:rgba(28,28,36,.72)}}"
"*{box-sizing:border-box}html{color-scheme:light dark}"   /* native date/checkbox/scrollbars follow the theme */
"body{font:16px/1.45 system-ui,sans-serif;color:var(--fg);background:var(--bg);margin:0}"
"#bar{position:sticky;top:0;z-index:5;padding:12px 16px;background:var(--barbg);"
"backdrop-filter:blur(14px);-webkit-backdrop-filter:blur(14px);border-bottom:1px solid var(--line)}"
".titlerow{display:flex;align-items:baseline;gap:.5rem}"
".brand{font-size:1.35rem;font-weight:700}.muted{color:var(--muted)}"
"#count{margin-left:auto;font-size:.8rem}"
".searchrow{display:flex;align-items:center;margin-top:.6rem;background:var(--card);"
"border:1px solid var(--line);border-radius:28px;padding:0 .8rem}"
"#q{flex:1;font:inherit;border:0;outline:none;background:transparent;padding:.7rem .4rem}"
"#out{max-width:720px;margin:0 auto;padding:.5rem 1rem 7rem}"
".hit{padding:.85rem .2rem;border-bottom:1px solid var(--line);word-break:break-word}"
".hit:last-child{border-bottom:0}.hit a{color:var(--accent);text-decoration:underline}"
".empty{color:var(--muted);text-align:center;margin-top:3rem}"
".storerow{font-size:.72rem;margin-top:.45rem;display:flex;gap:.4rem;align-items:center}"
".link{border:0;background:none;color:var(--accent);cursor:pointer;font:inherit;text-decoration:underline;padding:0}"
".fab{position:fixed;right:18px;bottom:78px;border:0;border-radius:30px;padding:.9rem 1.3rem;"
"cursor:pointer;font:inherit;font-weight:600;color:#fff;background:var(--accent);box-shadow:0 4px 14px rgba(0,0,0,.25)}"
"#bnav{position:fixed;left:0;right:0;bottom:0;z-index:6;display:flex;background:var(--card);border-top:1px solid var(--line);box-shadow:0 -2px 12px rgba(0,0,0,.05)}"
"#bnav button{flex:1;border:0;background:none;padding:.5rem 0 .62rem;font:inherit;font-size:.72rem;color:var(--muted);cursor:pointer;display:flex;flex-direction:column;align-items:center;gap:.12rem}"
"#bnav button .ic{font-size:1.3rem;line-height:1}#bnav button.on{color:var(--accent)}"
"#sheet,#syncsheet,#editsheet{position:fixed;inset:0;z-index:10;background:rgba(0,0,0,.35);display:flex;align-items:center;justify-content:center}"
"#sheet[hidden],#syncsheet[hidden],#editsheet[hidden]{display:none}"
".card{width:100%;max-width:560px;background:var(--card);border-radius:18px;padding:1.2rem;margin:1rem}"
".card h2{margin:0 0 1rem;font-size:1.15rem}"
".card textarea,.card input{width:100%;font:inherit;padding:.7rem .8rem;border:1px solid var(--line);"
"border-radius:10px;margin-bottom:.8rem;background:var(--field)}"
".actions{display:flex;justify-content:flex-end;gap:.6rem}"
".actions button{font:inherit;padding:.6rem 1.1rem;border-radius:10px;cursor:pointer}"
".ghost{border:1px solid var(--line);background:var(--card)}"
".primary{border:0;background:var(--accent);color:#fff;font-weight:600}"
".getbtn{border:0;background:var(--accent);color:#fff;font:inherit;font-weight:600;"
"padding:.55rem 1.3rem;border-radius:22px;cursor:pointer;white-space:nowrap}"
".minibtn{border:1px solid var(--line);background:var(--card);color:var(--muted);font:inherit;"
"font-size:.78rem;padding:.28rem .8rem;border-radius:8px;cursor:pointer}"
".minibtn.active{background:var(--accent);color:#fff;border-color:var(--accent)}"
".daygroup{font-size:.85rem;color:var(--fg);font-weight:700;text-transform:uppercase;letter-spacing:.04em;"
"margin:1.1rem 0 .2rem;padding-bottom:.2rem;border-bottom:1px solid var(--line)}"
".meta{font-size:.8rem;color:var(--muted);margin-top:.2rem}"
".tagrow{display:flex;align-items:center;justify-content:space-between;padding:.55rem .2rem;border-bottom:1px solid var(--line)}"
".taglink{border:0;background:none;color:var(--accent);font:inherit;cursor:pointer;padding:0;text-align:left;word-break:break-word}"
".tagcount{font-size:.8rem;color:var(--muted);background:var(--card);border:1px solid var(--line);border-radius:10px;padding:.1rem .5rem;min-width:2rem;text-align:center}"
".act{display:flex;gap:.8rem;margin-top:.35rem}.actmenu{display:inline-flex;gap:.8rem}.actmenu[hidden]{display:none}"
".actbtn{border:0;background:none;color:var(--muted);font:inherit;font-size:.8rem;cursor:pointer;padding:0}"
".actbtn:hover{color:var(--accent)}"
".actbtn.del:hover{color:#c0392b}"
".loadmore{display:block;width:100%;margin:1rem 0;padding:.6rem;border:1px solid var(--line);background:var(--card);color:var(--accent);border-radius:8px;cursor:pointer;font:inherit}"
".loadmore:hover{background:var(--field)}"
".chips{display:flex;flex-wrap:wrap;gap:.4rem;margin:.2rem 0 .7rem}"
".chip{display:inline-flex;align-items:center;gap:.35rem;background:var(--field);border:1px solid var(--line);border-radius:16px;padding:.25rem .7rem;font-size:.9rem}"
".chip button{border:0;background:none;color:var(--muted);cursor:pointer;font:inherit;font-size:1rem;line-height:1;padding:0}"
".chip button:hover{color:#c0392b}"
"</style>"
"<header id=bar><div class=titlerow><span class=brand>AIS</span><span id=count class=muted></span></div>"
"<div class=searchrow><input id=q type=search placeholder='type tags to filter' autocomplete=off autofocus>"
"<button id=seg-recall class=getbtn>Search</button></div>"
"<label class=allk style='display:flex;align-items:center;gap:.4rem;font-size:.85rem;color:var(--muted);margin-top:.5rem'><input id=anyk type=checkbox style='width:auto'> Match any tag</label>"
"<div class=storerow><span id=store class=muted></span><span style='flex:1'></span>"
"<button id=syncbtn class=link>sync</button><button id=storebtn class=link>change</button></div>"
"<div id=tlrange style='display:none;gap:.4rem;align-items:center;margin-top:.5rem;font-size:.85rem;color:var(--muted)'>"
"<span>from</span><input id=tlfrom type=date style='font:inherit'>"
"<span>to</span><input id=tlto type=date style='font:inherit'>"
"<button id=tlclear class=link>clear</button></div></header>"
"<main id=out><p class=empty>Loading...</p></main>"
"<button id=addbtn class=fab>+ Add</button>"
"<nav id=bnav><button data-v=recall><span class=ic>&#128269;</span>Search</button>"
"<button data-v=timeline class=on><span class=ic>&#128336;</span>Recent</button>"
"<button data-v=tags><span class=ic>&#127991;</span>Tags</button></nav>"
"<div id=sheet hidden><div class=card><h2>Add to your memory</h2>"
"<input id=vk placeholder='Tags (space or comma separated, optional)'>"
"<textarea id=v rows=3 placeholder='What to remember: a link, a note, a number...'></textarea>"
"<div class=encrow style='display:flex;align-items:center;gap:.5rem;margin:.1rem 0'>"
"<label style='display:flex;align-items:center;gap:.35rem;font-size:.85rem;color:var(--muted);white-space:nowrap'>"
"<input id=enc type=checkbox style='width:auto'> Encrypt</label>"
"<input id=pp type=password placeholder='Passphrase' hidden style='flex:1'></div>"
"<div class=actions><button id=cancel class=ghost>Cancel</button><button id=save class=primary>Save</button></div>"
"</div></div>"
/* Edit modal: in-place value edit + a chip tag editor (Flutter parity, and it
 * replaces the old -key prompt). The value box is hidden for encrypted/blob rows. */
"<div id=editsheet hidden><div class=card><h2>Edit</h2>"
"<div id=edvalwrap><label class=muted style='font-size:.8rem'>Value</label>"
"<textarea id=edval rows=3></textarea></div>"
"<label class=muted style='font-size:.8rem'>Tags</label>"
"<div id=edchips class=chips></div>"
"<input id=edtag placeholder='Add a tag (space or comma to add)'>"
"<div class=actions><button id=edcancel class=ghost>Cancel</button><button id=edsave class=primary>Save</button></div>"
"</div></div>"
/* Sync sheet: mirrors the mobile Sync (Host / Join). One device Hosts and waits;
 * the other Joins with its address + token. Both converge (bidirectional). */
"<div id=syncsheet hidden><div class=card>"
"<h2>Sync with another device</h2>"
"<p class=muted style='margin:.2rem 0 1rem;font-size:.9rem'>Both devices end up with the same records. One hosts and waits; the other joins it. Same Wi-Fi.</p>"
"<div id=syncpick class=actions style='justify-content:center'>"
"<button id=syncjoinbtn class=ghost>Join</button><button id=synchostbtn class=primary>Host</button></div>"
/* File transfer: no network -- export the whole index to a file, or merge one in
 * (move it by Drive / USB / email). Mirrors the app's Export / Import. */
"<div id=syncfile><p class=muted style='margin:1rem 0 .5rem;font-size:.85rem'>Or a file &mdash; move it by Drive, USB or email:</p>"
"<div class=actions style='justify-content:center'><button id=impbtn class=ghost>Import</button><button id=expbtn class=primary>Export</button></div></div>"
"<input id=fileimp type=file accept='.aisb' hidden>"
/* Host pane: address + token to read off, a QR to scan, and a live status line. */
"<div id=synchost hidden>"
"<p style='margin:.2rem 0 .6rem;font-size:.9rem'>Scan with the AIS app, or on the other device choose Join and enter:</p>"
"<div id=qr style='display:flex;justify-content:center;margin:.4rem 0 .8rem'></div>"
"<div style='font-family:monospace;font-size:.85rem;word-break:break-all;background:var(--field);border:1px solid var(--line);border-radius:10px;padding:.6rem'>"
"<div id=hostaddr></div><div id=hosttok class=muted></div></div>"
"<p id=hoststatus class=muted style='margin:.6rem 0 0;font-size:.9rem'>Waiting for the other device...</p></div>"
/* Join pane: address + token inputs and a status line. */
"<div id=syncjoin hidden>"
"<input id=jaddr placeholder='Address' value='http://' style='margin-bottom:.6rem'>"
"<input id=jtok placeholder='Token' style='margin-bottom:.6rem'>"
"<p class=muted style='margin:0 0 .6rem;font-size:.85rem'>On the other device: open Sync, choose Host, and read off its address and token.</p>"
"<div class=actions><button id=jgo class=primary>Sync</button></div>"
"<p id=joinstatus class=muted style='margin:.6rem 0 0;font-size:.9rem'></p></div>"
"<div class=actions style='margin-top:1rem'><button id=synccancel class=ghost>Close</button></div>"
"</div></div>"
"<script>"
"var $=function(i){return document.getElementById(i)};"
"var view='recall';"
/* empty-state call-to-action, reused by every empty view */
"var addCTA='<button class=primary style=\"margin-top:1rem\" onclick=openSheet()>+ Add</button>';"
/* accept a comma as an optional tag separator; collapse extra whitespace, so
 * \"home, wifi\" and \"home   wifi\" both mean the tags home + wifi (Flutter parity) */
"function normkeys(s){return s.replace(/,/g,' ').trim().replace(/\\s+/g,' ')}"
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
"node.innerHTML='';if(t){fillVal(node,t);var c=document.createElement('button');c.className='actbtn';c.textContent='copy';c.style.marginLeft='.5rem';c.onclick=function(){copyText(t,c)};node.appendChild(c)}else{node.textContent='(cannot decrypt)'}}"
/* A document blob comes over the wire as "aisdoc:<base64 of the content>", so
 * the (possibly multi-line) content survives the line-based record split. Decode
 * the base64 as UTF-8 bytes and show it verbatim (newlines preserved). */
"function docText(v){try{var s=atob(v.slice(7)),b=new Uint8Array(s.length),i;for(i=0;i<s.length;i++)b[i]=s.charCodeAt(i);return new TextDecoder().decode(b)}catch(e){return v}}"
"function fillDoc(node,v){var d=document.createElement('div');d.style.whiteSpace='pre-wrap';d.textContent=docText(v);node.appendChild(d)}"
"function render(t,q,ms){var L=t.split('\\n').filter(function(s){return s.length});"
"$('count').textContent=L.length+' result'+(L.length==1?'':'s')+' - '+ms.toFixed(0)+' ms';"
"var o=$('out');o.innerHTML='';"
"if(!L.length){o.textContent='No results for '+q;o.className='empty';return}o.className='';"
"L.forEach(function(ln){var p=ln.indexOf('|'),id=p>=0?ln.slice(0,p):'',v=p>=0?ln.slice(p+1):ln,"
"r=document.createElement('div');r.className='hit';"
"(v.indexOf('aisc:')==0?fillSecret(r,v):v.indexOf('aisdoc:')==0?fillDoc(r,v):fillVal(r,v));if(id)r.appendChild(rowActions(id,v));o.appendChild(r)})}"
/* per-row edit (attach/detach keys by id) and delete; both refresh the view */
"function rowActions(id,v){var d=document.createElement('div');d.className='act';"
"var m=document.createElement('button');m.className='actbtn';m.textContent='\\u22EE';m.title='more';"
"var box=document.createElement('span');box.className='actmenu';box.hidden=true;"
"if(v.indexOf('aisc:')!=0){var c=document.createElement('button');c.className='actbtn';c.textContent='copy';c.onclick=function(){copyText(v.indexOf('aisdoc:')==0?docText(v):v,c)};box.appendChild(c)}"
"var e=document.createElement('button');e.className='actbtn';e.textContent='edit';e.onclick=function(){openEdit(id,v)};"
"var x=document.createElement('button');x.className='actbtn del';x.textContent='\\u2715 delete';x.onclick=function(){delRec(id)};"
"box.appendChild(e);box.appendChild(x);m.onclick=function(){box.hidden=!box.hidden};"
"d.appendChild(m);d.appendChild(box);return d}"
"async function copyText(t,btn){try{await navigator.clipboard.writeText(t);if(btn){var o=btn.textContent;btn.textContent='copied';setTimeout(function(){btn.textContent=o},1200)}}catch(e){alert('copy needs https or localhost')}}"
"async function delRec(id){if(!confirm('Delete this record?'))return;"
"await fetch('/api/del?id='+id,{method:'POST'});setView(view)}"
/* Edit modal: value (when editable) + a chip tag editor, computing a minimal
 * +tag/-tag delta on save. Replaces the old -KEY grammar prompt. */
"var edId=0,edTags=[],edOldVal='',edEdit=false;"
"function edChips(){var c=$('edchips');c.innerHTML='';edTags.forEach(function(t){"
"var s=document.createElement('span');s.className='chip';s.textContent=t;"
"var b=document.createElement('button');b.textContent='\\u00d7';b.onclick=function(){edTags=edTags.filter(function(x){return x!=t});edChips()};"
"s.appendChild(b);c.appendChild(s)})}"
"function edAdd(){normkeys($('edtag').value).split(' ').forEach(function(t){if(t&&edTags.indexOf(t)<0)edTags.push(t)});$('edtag').value='';edChips()}"
"async function openEdit(id,v){edId=id;edOldVal=v;edEdit=(v.indexOf('aisc:')!=0&&v.indexOf('aisdoc:')!=0);"
"$('edvalwrap').style.display=edEdit?'block':'none';if(edEdit)$('edval').value=v;"
"var t=(await(await fetch('/api/keys?id='+id)).text()).trim();edTags=t?t.split(/\\s+/):[];edChips();"
"$('edtag').value='';$('editsheet').hidden=false}"
"async function edSave(){edAdd();"
"if(edEdit){var nv=$('edval').value.replace(/\\r?\\n/g,' ').trim();if(nv&&nv!=edOldVal)await fetch('/api/setvalue?id='+edId,{method:'POST',body:edOldVal+'\\n'+nv})}"
"var o=(await(await fetch('/api/keys?id='+edId)).text()).trim(),oa=o?o.split(/\\s+/):[],dl=[];"
"oa.forEach(function(t){if(edTags.indexOf(t)<0)dl.push('-'+t)});edTags.forEach(function(t){if(oa.indexOf(t)<0)dl.push(t)});"
"if(dl.length)await fetch('/api/update?id='+edId+'&keys='+encodeURIComponent(dl.join(' ')),{method:'POST'});"
"$('editsheet').hidden=true;setView(view)}"
"async function recall(){var q=normkeys($('q').value);if(!q)return;var t0=performance.now();"
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
"if(!tlN&&!L.length){o.innerHTML='<div class=empty><p>Nothing saved yet.</p>'+addCTA+'</div>';return}"
"L.forEach(function(ln){var r=parseTL(ln),lt=locDT(r.ts),d=lt?lt.day:'';"
"if(d!==tlDay){tlDay=d;var h=document.createElement('div');h.className='daygroup';"
"h.textContent=d?fmtDay(d):'(undated)';o.appendChild(h)}"
"var row=document.createElement('div');row.className='hit';(r.value.indexOf('aisdoc:')==0?fillDoc(row,r.value):fillVal(row,r.value));"
"var m=document.createElement('div');m.className='meta';"
"var tm=lt&&lt.time?lt.time+' - ':'';"
"m.textContent=tm+(r.keys||'(no tags)');row.appendChild(m);"
"if(r.id){row.appendChild(rowActions(r.id,r.value));tlBefore=r.id}o.appendChild(row)});"
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
"function setView(v){view=v;"
"[].forEach.call(document.querySelectorAll('#bnav button'),function(b){b.className=(b.dataset.v==v)?'on':''});"
"$('tlrange').style.display=(v=='timeline')?'flex':'none';"   /* date range only in Timeline */
"if(v=='recall'){var q=$('q').value.trim();if(q)recall();"
"else{$('out').innerHTML='<div class=empty><p>Type tags, then Search.</p>'+addCTA+'</div>';$('out').className='';$('count').textContent=''}}"
"else if(v=='timeline')loadTimeline();else loadTags()}"
"function openSheet(){$('vk').value=$('q').value.trim();$('enc').checked=false;$('pp').value='';$('pp').hidden=true;$('sheet').hidden=false;$('v').focus()}"
"function closeSheet(){$('sheet').hidden=true;$('v').value='';$('pp').value=''}"
"async function save(){var v=$('v').value.trim();if(!v)return;var k=normkeys($('vk').value);"
"var enc=$('enc').checked,pp=$('pp').value;"
"if(enc&&!pp){alert('Enter a passphrase to encrypt');return}"
/* On a failed or unreachable put, tell the user and KEEP the sheet so they can
 * retry -- never leave a stuck, silent modal (an unhandled await used to). */
"try{var r=await fetch('/api/put?keys='+encodeURIComponent(k)+(enc?'&enc=1':''),{method:'POST',body:enc?(pp+'\\n'+v):v});"
"if(!r.ok)throw new Error('server '+r.status)}catch(e){alert('Save failed ('+e.message+'). Nothing was saved.');return}"
"closeSheet();$('q').value=k;recall()}"
"$('q').addEventListener('keydown',function(e){if(e.key=='Enter')setView('recall')});"
"$('seg-recall').onclick=function(){setView('recall')};"
"[].forEach.call(document.querySelectorAll('#bnav button'),function(b){b.onclick=function(){setView(b.dataset.v)}});"
"$('tlfrom').onchange=function(){loadTimeline()};$('tlto').onchange=function(){loadTimeline()};"
"$('tlclear').onclick=function(){$('tlfrom').value='';$('tlto').value='';loadTimeline()};"
"$('addbtn').onclick=openSheet;$('cancel').onclick=closeSheet;$('save').onclick=save;"
"$('edcancel').onclick=function(){$('editsheet').hidden=true};$('edsave').onclick=edSave;"
"$('edtag').addEventListener('keydown',function(e){if(e.key=='Enter'||e.key==','){e.preventDefault();edAdd()}});"
"$('edtag').addEventListener('blur',edAdd);"
"$('editsheet').addEventListener('click',function(e){if(e.target==$('editsheet'))$('editsheet').hidden=true});"
"$('enc').onchange=function(){$('pp').hidden=!$('enc').checked;if($('enc').checked)$('pp').focus()};"
"$('sheet').addEventListener('click',function(e){if(e.target==$('sheet'))closeSheet()});"
"async function loadStore(){$('store').textContent='Library: '+await(await fetch('/api/where')).text()}"
"async function changeStore(){var cur=await(await fetch('/api/where')).text();"
"var d=prompt('Library folder (full path):',cur);if(!d||d==cur)return;"
"var r=await fetch('/api/store',{method:'POST',body:d});"
"if(r.ok){$('q').value='';$('out').innerHTML='';$('count').textContent='';loadStore()}"
"else{alert('Could not open that index')}}"
"$('storebtn').onclick=changeStore;loadStore();"
"setView('timeline');"   /* open on content (Recent), not a blank search */
/* ---- Sync (Host / Join), mirroring the mobile Sync feature ---------------
 * qrGen: a small pure-JS byte-mode QR encoder (ECC level L, mask 0, versions
 * 1-10) so a phone can scan the url+token with no server-side or network
 * dependency. Returns a matrix of 0/1 modules. */
"function qrGen(s){var EXP=new Array(512),LOG=new Array(256),x=1,i;"
"for(i=0;i<255;i++){EXP[i]=x;LOG[x]=i;x<<=1;if(x&256)x^=285}"
"for(i=255;i<512;i++)EXP[i]=EXP[i-255];"
"function mul(a,b){return(a===0||b===0)?0:EXP[LOG[a]+LOG[b]]}"
"function rsGen(n){var g=[1],j;for(i=0;i<n;i++){var ng=new Array(g.length+1).fill(0);"
"for(j=0;j<g.length;j++){ng[j]^=mul(g[j],EXP[i]);ng[j+1]^=g[j]}g=ng}return g}"
"var CAP=[[19,7],[34,10],[55,15],[80,20],[108,26],[136,18],[156,20],[194,24],[232,30],[274,18]];"
"var bytes=[];for(i=0;i<s.length;i++)bytes.push(s.charCodeAt(i)&255);"
"var ver=-1,dataCW=0,eccCW=0;"
"for(i=0;i<CAP.length;i++){var cw=CAP[i][0];"
"if(4+8+8*bytes.length<=cw*8){ver=i+1;dataCW=cw;eccCW=CAP[i][1];break}}"
"if(ver<0)throw'payload too big';"
"var bits=[];function put(v,n){for(var b=n-1;b>=0;b--)bits.push((v>>b)&1)}"
"put(4,4);put(bytes.length,8);for(i=0;i<bytes.length;i++)put(bytes[i],8);"
"var cap=dataCW*8;var t=Math.min(4,cap-bits.length);for(i=0;i<t;i++)bits.push(0);"
"while(bits.length%8!==0)bits.push(0);"
"var data=[];for(i=0;i<bits.length;i+=8){var v=0;for(var j=0;j<8;j++)v=(v<<1)|bits[i+j];data.push(v)}"
"var pad=[236,17],pi=0;while(data.length<dataCW){data.push(pad[pi&1]);pi++}"
"var gen=rsGen(eccCW),ecc=new Array(eccCW).fill(0);"
"for(i=0;i<data.length;i++){var f=data[i]^ecc[0];ecc.shift();ecc.push(0);"
"if(f!==0)for(var j=0;j<eccCW;j++)ecc[j]^=mul(gen[j+1],f)}"
"var all=data.concat(ecc);"
"var size=17+4*ver,m=[],used=[];for(i=0;i<size;i++){m.push(new Array(size).fill(0));used.push(new Array(size).fill(0))}"
"function set(r,c,v){m[r][c]=v?1:0;used[r][c]=1}"
"function finder(r,c){for(var dr=-1;dr<=7;dr++)for(var dc=-1;dc<=7;dc++){var rr=r+dr,cc=c+dc;"
"if(rr<0||cc<0||rr>=size||cc>=size)continue;"
"var on=(dr>=0&&dr<=6&&(dc===0||dc===6))||(dc>=0&&dc<=6&&(dr===0||dr===6))||(dr>=2&&dr<=4&&dc>=2&&dc<=4);"
"set(rr,cc,on?1:0)}}"
"finder(0,0);finder(0,size-7);finder(size-7,0);"
"for(i=8;i<size-8;i++){if(!used[6][i])set(6,i,(i%2===0)?1:0);if(!used[i][6])set(i,6,(i%2===0)?1:0)}"
"var AL=[[],[],[6,18],[6,22],[6,26],[6,30],[6,34],[6,22,38],[6,24,42],[6,26,46],[6,28,50]];"
"var al=AL[ver];for(var a=0;a<al.length;a++)for(var b=0;b<al.length;b++){var r=al[a],c=al[b];"
"if(used[r][c])continue;for(var dr=-2;dr<=2;dr++)for(var dc=-2;dc<=2;dc++){var on=(Math.abs(dr)===2||Math.abs(dc)===2||(dr===0&&dc===0));set(r+dr,c+dc,on?1:0)}}"
"set(size-8,8,1);"
"for(i=0;i<9;i++){if(!used[8][i])used[8][i]=2;if(!used[i][8])used[i][8]=2}"
"for(i=0;i<8;i++){used[8][size-1-i]=2;used[size-1-i][8]=2}"
"if(ver>=7){for(var vr=0;vr<6;vr++)for(var vc=0;vc<3;vc++){used[vr][size-11+vc]=2;used[size-11+vc][vr]=2}}"
"var idx=0,dir=-1,col;"
"for(col=size-1;col>0;col-=2){if(col===6)col--;"
"for(var t2=0;t2<size;t2++){var row=(dir<0)?(size-1-t2):t2;"
"for(var q=0;q<2;q++){var cc=col-q;"
"if(used[row][cc])continue;"
"var bit=(idx<all.length*8)?((all[idx>>3]>>(7-(idx&7)))&1):0;idx++;"
"if(((row+cc)%2)===0)bit^=1;"
"set(row,cc,bit)}}dir=-dir}"
"var fmt=0x77c4,fb=[];for(i=14;i>=0;i--)fb.push((fmt>>i)&1);"
"var fp1=[[8,0],[8,1],[8,2],[8,3],[8,4],[8,5],[8,7],[8,8],[7,8],[5,8],[4,8],[3,8],[2,8],[1,8],[0,8]];"
"for(i=0;i<15;i++)m[fp1[i][0]][fp1[i][1]]=fb[i];"
"var fp2=[[size-1,8],[size-2,8],[size-3,8],[size-4,8],[size-5,8],[size-6,8],[size-7,8],[8,size-8],[8,size-7],[8,size-6],[8,size-5],[8,size-4],[8,size-3],[8,size-2],[8,size-1]];"
"for(i=0;i<15;i++)m[fp2[i][0]][fp2[i][1]]=fb[i];return m}"
/* qrDraw: paint the matrix into EL as a crisp canvas with a 4-module quiet zone. */
"function qrDraw(el,text){el.innerHTML='';try{var M=qrGen(text),n=M.length,q=4,sc=6,px=(n+2*q)*sc;"
"var cv=document.createElement('canvas');cv.width=px;cv.height=px;cv.style.width=px+'px';cv.style.height=px+'px';"
"var g=cv.getContext('2d');g.fillStyle='#fff';g.fillRect(0,0,px,px);g.fillStyle='#000';"
"for(var r=0;r<n;r++)for(var c=0;c<n;c++)if(M[r][c])g.fillRect((c+q)*sc,(r+q)*sc,sc,sc);"
"el.appendChild(cv)}catch(e){el.textContent='(QR unavailable)'}}"
/* the Sync sheet: a role picker, then a Host pane (address+token+QR, polling) or
 * a Join pane (address+token inputs). Both converge (the bidir exchange). */
"var syncPoll=null;"
"function openSync(){$('syncpick').style.display='flex';$('syncfile').style.display='block';$('synchost').hidden=true;$('syncjoin').hidden=true;$('syncsheet').hidden=false}"
"function closeSync(){$('syncsheet').hidden=true;if(syncPoll){clearInterval(syncPoll);syncPoll=null}}"
"async function syncHost(){$('syncpick').style.display='none';$('syncfile').style.display='none';$('synchost').hidden=false;"
"$('hoststatus').textContent='Starting...';"
"var r=await fetch('/api/sync/host',{method:'POST'});"
"if(!r.ok){$('hoststatus').textContent='Could not start host. Is one already running?';return}"
"var t=(await r.text()).split('\\n'),url=t[0],tok=t[1];"
"$('hostaddr').textContent=url;$('hosttok').textContent='token: '+tok;"
"qrDraw($('qr'),'ais://sync?host='+encodeURIComponent(url.split('://')[1])+'&token='+tok);"
"$('hoststatus').textContent='Waiting for the other device...';"
"if(syncPoll)clearInterval(syncPoll);"
"syncPoll=setInterval(async function(){var s=(await(await fetch('/api/sync/status')).text()).trim();"
"if(s=='synced'){clearInterval(syncPoll);syncPoll=null;$('hoststatus').textContent='Synced. Both devices now have the same records.';setView(view)}"
"else if(s=='timeout'){clearInterval(syncPoll);syncPoll=null;$('hoststatus').textContent='No device joined in time. Try again.'}},1500)}"
"function syncJoinPane(){$('syncpick').style.display='none';$('syncfile').style.display='none';$('syncjoin').hidden=false;$('joinstatus').textContent='';$('jaddr').focus()}"
/* file export/import: no network -- carry the whole index as one .aisb file */
"function fileExport(){var a=document.createElement('a');a.href='/api/export-bundle';a.download='ais-export.aisb';document.body.appendChild(a);a.click();a.remove()}"
"async function fileImport(f){var b=await f.arrayBuffer();var r=await fetch('/api/import-bundle',{method:'POST',body:b});"
"if((await r.text()).trim()=='merged'){closeSync();setView(view);alert('Imported. Records merged.')}else{alert('Import failed. Is it an .aisb file from Export?')}}"
"async function syncJoinGo(){var url=$('jaddr').value.trim(),tok=$('jtok').value.trim();"
"if(!url||!tok){$('joinstatus').textContent='Enter an address and a token.';return}"
"$('joinstatus').textContent='Syncing...';"
"var r=await fetch('/api/sync/join',{method:'POST',body:url+'\\n'+tok});var s=(await r.text()).trim();"
"if(s=='merged'){$('joinstatus').textContent='Synced. Both devices now have the same records.';setView(view)}"
"else if(s=='bad url')$('joinstatus').textContent='That address looks wrong. Use http://host:port.';"
"else $('joinstatus').textContent='Could not sync. Same Wi-Fi? Check the host is waiting and the token is right.'}"
"$('syncbtn').onclick=openSync;$('synccancel').onclick=closeSync;"
"$('synchostbtn').onclick=syncHost;$('syncjoinbtn').onclick=syncJoinPane;$('jgo').onclick=syncJoinGo;"
"$('expbtn').onclick=fileExport;$('impbtn').onclick=function(){$('fileimp').click()};"
"$('fileimp').onchange=function(){if(this.files[0])fileImport(this.files[0]);this.value=''};"
"$('syncsheet').addEventListener('click',function(e){if(e.target==$('syncsheet'))closeSync()});"
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

/* A multi-line value is stored as a plain-text document blob (blobs/<ts>.txt)
 * whose PATH is the record value; the GUI must show the CONTENT, not the path.
 * If VALUE is such a blob, read it (capped) and return "aisdoc:<base64>" in OUT:
 * base64 carries the bytes with no newline or '|', so the line-based wire and
 * the client's record split stay intact; the client decodes and renders it.
 * Otherwise return VALUE unchanged. Single-threaded server, so a static read
 * buffer is safe. */
static const char *show_value(ais *a, const char *value, char *out, size_t outsz)
{
    static char content[AIS_LINE_MAX / 2];        /* preview cap, one shared resolver */
    long got = ais_doc_display(a, value, content, sizeof content);

    if (got < 0)
        return value;                             /* not a document blob (or absent): as-is */
    if (outsz < 7 + AIS_B64_ENCLEN((size_t)got))
        return value;                             /* won't fit: fall back to the path */
    memcpy(out, "aisdoc:", 7);
    return (b64_encode((const unsigned char *)content, (size_t)got, out + 7, outsz - 7) >= 0)
               ? out : value;
}

static int on_value(long id, const char *value, void *vp)
{
    struct sink *s = vp;
    static char vbuf[AIS_LINE_MAX];
    char line[AIS_LINE_MAX];
    const char *v = show_value(s->a, value, vbuf, sizeof vbuf);
    int n = snprintf(line, sizeof(line), "%ld|%s\n", id, v);
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

/* keys-of-id: which visible tags is record ID filed under? Mirrors the embed
 * layer's keysOf -- walk the tags, and for each ask ais_get whether ID is a
 * member. O(tags), but the web edit dialog needs the current tag set to show
 * its chips. Emits the matching tags space-separated. */
struct keyhit { long want; int found; };
static int keyhit_cb(long id, void *vp)
{
    struct keyhit *k = vp;
    if (id == k->want) { k->found = 1; return 1; }   /* found: stop the scan */
    return 0;
}
struct keysof { ais *a; long want; int fd; int n; };
static int keysof_tag(const char *key, long count, void *vp)
{
    struct keysof *c = vp;
    char kbuf[AIS_LINE_MAX];
    char *kv[1];
    struct keyhit m;
    (void)count;
    if (strlen(key) >= sizeof kbuf)
        return 0;                              /* skip an absurdly long key */
    strcpy(kbuf, key);                         /* ais_get tokenizes in place */
    kv[0] = kbuf;
    m.want = c->want; m.found = 0;
    ais_get(c->a, kv, 1, AIS_AND, keyhit_cb, &m);
    if (m.found) {
        if (c->n) write_all(c->fd, " ", 1);
        write_all(c->fd, key, strlen(key));
        c->n++;
    }
    return 0;
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
    static char vbuf[AIS_LINE_MAX];
    char line[AIS_LINE_MAX];
    const char *v = show_value(s->a, value, vbuf, sizeof vbuf);   /* document blob -> content */
    int n = snprintf(line, sizeof(line), "%ld|%s|%s|%s\n", id, ts, keys, v);
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

#ifdef SERVE_HAVE_SYNC
/* ---- LAN sync (Host / Join), mirroring the mobile Sync feature -----------
 * Host: fork an ephemeral child that runs sync_serve() single-shot, so the
 * single-threaded HTTP loop is not blocked while it waits for a peer; the
 * parent returns the pairing info (URL + token) at once and the page renders a
 * QR of it. The child exits after one peer or the timeout; the parent reaps it.
 * Join: synchronous sync_pull() -- a LAN merge is quick. Both use bidir=1, the
 * symmetric exchange the mobile app uses (both devices converge in one round).
 */
#define SERVE_SYNC_BIDIR    1     /* symmetric exchange (matches the mobile Sync) */
#define SERVE_SYNC_TIMEOUT 120    /* seconds the host child waits for one peer    */

static volatile sig_atomic_t sync_child = -1;   /* live Host child pid, or -1 (one at a time) */
static volatile sig_atomic_t sync_last  = -2;   /* last Host outcome: 0 served, else not      */

/* Reap the Host child if it has finished so it leaves no zombie, remembering its
 * outcome (the child exits 0 when a peer synced, non-zero on timeout/error).
 * Called both from a SIGCHLD handler (a child dying between polls) and from the
 * routes; WNOHANG and the pid guard make a double call harmless. Only
 * waitpid-and-plain-assignment here, so it is async-signal-safe. */
static void sync_reap(void)
{
    pid_t pid = (pid_t)sync_child;
    int st;
    if (pid <= 0)
        return;
    if (waitpid(pid, &st, WNOHANG) == pid) {
        sync_last = (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : -2;
        sync_child = -1;
    }
}

static void sync_on_sigchld(int sig)
{
    (void)sig;
    sync_reap();                               /* async-signal-safe: only waitpid */
}

/* The primary LAN IPv4, via connecting a UDP socket (no packet is sent). Same
 * trick as the engine's CLI wrapper; kept local so this stays a pure GUI caller. */
static int sync_lan_ip(char *buf, size_t n)
{
    int fd;
    struct sockaddr_in to, me;
    socklen_t ml = sizeof me;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &to.sin_addr);
    if (connect(fd, (struct sockaddr *)&to, sizeof to) != 0) { close(fd); return -1; }
    if (getsockname(fd, (struct sockaddr *)&me, &ml) != 0) { close(fd); return -1; }
    close(fd);
    return (inet_ntop(AF_INET, &me.sin_addr, buf, n) == NULL) ? -1 : 0;
}

/* Host: generate a token, find this device's LAN IP, fork a child that serves one
 * peer, and reply "http://ip:port\ntoken\n" so the page shows it and draws a QR.
 * The child exits after one peer or the timeout; /api/sync/status reaps it. */
static void sync_host(ais *a, int fd)
{
    char ip[64], token[33], reply[160];
    int port = AIS_SYNC_PORT, m;
    pid_t pid;

    sync_reap();                              /* clear a finished previous Host */
    if (sync_child > 0) {                      /* one at a time */
        static const char e[] = "HTTP/1.0 409 Conflict\r\nConnection: close\r\n\r\n"
            "a sync host is already waiting\n";
        write_all(fd, e, sizeof(e) - 1);
        return;
    }
    if (aisc_token(token, sizeof token) != AISC_OK ||
        sync_lan_ip(ip, sizeof ip) != 0) {
        static const char e[] = "HTTP/1.0 500 Internal Server Error\r\n"
            "Connection: close\r\n\r\ncould not start host\n";
        write_all(fd, e, sizeof(e) - 1);
        return;
    }
    pid = fork();
    if (pid == 0) {                            /* child: serve one peer, then exit */
        ais fresh;
        int rc;
        signal(SIGCHLD, SIG_DFL);              /* drop the parent's reaper: its handler */
        SOCK_CLOSE(fd);                        /* would EINTR sync_serve's poll(). Also  */
                                               /* close the client fd we no longer use. */
        /* Fresh handle to the same dir, not the inherited one: separate OFDs, so the
         * store's file lock serializes this merge against the parent's writes. */
        if (ais_open(&fresh, a->dir) != 0)
            _exit(1);
        rc = sync_serve(&fresh, port, token, SERVE_SYNC_TIMEOUT, SERVE_SYNC_BIDIR);
        ais_close(&fresh);
        _exit(rc == 0 ? 0 : 1);                /* 0 = a peer synced; 1 = timeout/error */
    }
    if (pid < 0) {
        static const char e[] = "HTTP/1.0 500 Internal Server Error\r\n"
            "Connection: close\r\n\r\ncould not start host\n";
        write_all(fd, e, sizeof(e) - 1);
        return;
    }
    sync_child = pid;
    sync_last = -1;                            /* -1 = still waiting */
    m = snprintf(reply, sizeof reply, "http://%s:%d\n%s\n", ip, port, token);
    send_head(fd, "text/plain");
    if (m > 0)
        write_all(fd, reply, (size_t)m);
}

/* Status: reap a finished Host and report "waiting" / "synced" / "timeout" so the
 * page can poll and tell the user when a peer completed. */
static void sync_status(int fd)
{
    const char *s;
    sync_reap();
    if (sync_child > 0)      s = "waiting\n";
    else if (sync_last == 0) s = "synced\n";
    else                     s = "timeout\n";
    send_head(fd, "text/plain");
    write_all(fd, s, strlen(s));
}

/* Join: pull + merge from a peer's URL with its token (bidir, so the host also
 * converges). Reply "merged" / "bad url" / "could not connect or wrong token". */
static void sync_join(ais *a, char *body, int fd)
{
    char host[128], *tok;
    int port, rc;
    size_t bl;

    tok = strchr(body, '\n');                  /* body = "url\ntoken" */
    if (tok == NULL) {
        send_head(fd, "text/plain");
        write_all(fd, "bad url\n", 8);
        return;
    }
    *tok++ = '\0';
    bl = strlen(tok);                          /* trim any trailing CR/LF/space */
    while (bl > 0 && (tok[bl-1] == '\r' || tok[bl-1] == '\n' ||
                      tok[bl-1] == ' '  || tok[bl-1] == '\t'))
        tok[--bl] = '\0';
    if (sync_parse_url(body, host, sizeof host, &port) != 0) {
        send_head(fd, "text/plain");
        write_all(fd, "bad url\n", 8);
        return;
    }
    rc = sync_pull(a, host, port, tok, 10, SERVE_SYNC_BIDIR);   /* 10s LAN timeout */
    send_head(fd, "text/plain");
    if (rc == 0) write_all(fd, "merged\n", 7);
    else         write_all(fd, "could not connect or wrong token\n", 33);
}
#endif /* SERVE_HAVE_SYNC */

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
    long body_len = 0;                    /* Content-Length, for a big POST body      */
    int  count = 0;                       /* ?count= page size (0 => engine default)  */
    char *tlfrom = nokeys, *tlto = nokeys; /* ?from= ?to= date range (YYYY-MM-DD)      */

    n = SOCK_READ(fd, buf, sizeof(buf) - 1);   /* assume the request fits (a sketch) */
    if (n <= 0)
        return;
    buf[n] = '\0';

    /* Content-Length up front, from the header block only, before the request
     * line is tokenized in place (which plants NULs that would cut a later
     * strstr short). Needed for a big POST body like /api/import-bundle. */
    {
        char *he = strstr(buf, "\r\n\r\n");
        char keep = '\0';
        char *cl;
        if (he != NULL) { keep = *he; *he = '\0'; }
        cl = strstr(buf, "Content-Length:");
        if (cl == NULL) cl = strstr(buf, "content-length:");
        if (cl != NULL) body_len = atol(cl + 15);
        if (he != NULL) *he = keep;
    }

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
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/keys") == 0) {
        /* the visible tags of record ?id=N (for the edit dialog's chips) */
        struct keysof c;
        c.a = a; c.want = reqid; c.fd = fd; c.n = 0;
        send_head(fd, "text/plain");
        if (reqid > 0)
            ais_tags(a, keysof_tag, &c);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/setvalue") == 0) {
        /* body = "oldvalue\nnewvalue": rewrite record ?id=N's value in place,
         * keeping its id + timeline slot. Single-line values only (a multi-line
         * value lives out-of-line as a blob, which the UI won't offer to edit). */
        char *nv = (body != NULL) ? strchr(body, '\n') : NULL;
        if (reqid > 0 && nv != NULL) {
            *nv++ = '\0';
            if (ais_set_value(a, reqid, body, nv) == 0) {
                send_head(fd, "text/plain");
                write_all(fd, "updated\n", 8);
            } else {
                static const char e[] = "HTTP/1.0 400 Bad Request\r\n"
                    "Connection: close\r\n\r\ncannot update value\n";
                write_all(fd, e, sizeof(e) - 1);
            }
        } else {
            static const char e[] = "HTTP/1.0 400 Bad Request\r\n"
                "Connection: close\r\n\r\ncannot update value\n";
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
#ifdef SERVE_HAVE_SYNC
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/sync/host") == 0) {
        sync_host(a, fd);                      /* fork a child to serve one peer */
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/sync/status") == 0) {
        sync_status(fd);                       /* poll: waiting / synced / timeout */
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/sync/join") == 0) {
        sync_join(a, body, fd);                /* pull + merge from a host's url+token */
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/export-bundle") == 0) {
        /* Download the WHOLE index as one plaintext .aisb bundle (no passphrase):
         * carry it by Drive / USB / email and Import it elsewhere. Same format the
         * mobile/desktop app writes. */
        uint8_t *out = NULL;
        size_t len = 0;
        if (sync_export_plain(a, &out, &len) == 0) {
            char hdr[192];
            int h = snprintf(hdr, sizeof hdr,
                "HTTP/1.0 200 OK\r\nConnection: close\r\n"
                "Content-Type: application/octet-stream\r\n"
                "Content-Disposition: attachment; filename=\"ais-export.aisb\"\r\n"
                "Content-Length: %zu\r\n\r\n", len);
            if (h > 0)
                write_all(fd, hdr, (size_t)h);
            write_all(fd, (char *)out, len);
            free(out);
        } else {
            static const char e[] = "HTTP/1.0 500 Internal Server Error\r\n"
                "Connection: close\r\n\r\nexport failed\n";
            write_all(fd, e, sizeof(e) - 1);
        }
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/import-bundle") == 0) {
        /* Upload a plaintext .aisb bundle and merge it (same tombstone-union LWW
         * merge as live sync). The body can exceed the initial read buffer, so
         * read the full Content-Length. */
        long clen = body_len;
        if (clen <= 0 || clen > 64L * 1024 * 1024) {
            static const char e[] = "HTTP/1.0 400 Bad Request\r\n"
                "Connection: close\r\n\r\nbad upload\n";
            write_all(fd, e, sizeof(e) - 1);
        } else {
            uint8_t *data = malloc((size_t)clen);
            if (data == NULL) {
                static const char e[] = "HTTP/1.0 500 Internal Server Error\r\n"
                    "Connection: close\r\n\r\nout of memory\n";
                write_all(fd, e, sizeof(e) - 1);
            } else {
                size_t have = (size_t)(n - (body - buf));   /* body bytes already read */
                size_t got = have < (size_t)clen ? have : (size_t)clen;
                memcpy(data, body, got);
                while (got < (size_t)clen) {
                    ssize_t k = SOCK_READ(fd, (char *)data + got, (size_t)clen - got);
                    if (k <= 0) break;
                    got += (size_t)k;
                }
                if (got == (size_t)clen && sync_import_plain(a, data, (size_t)clen) == 0) {
                    send_head(fd, "text/plain");
                    write_all(fd, "merged\n", 7);
                } else {
                    static const char e[] = "HTTP/1.0 400 Bad Request\r\n"
                        "Connection: close\r\n\r\nimport failed\n";
                    write_all(fd, e, sizeof(e) - 1);
                }
                free(data);
            }
        }
#endif
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
#ifdef SERVE_HAVE_SYNC
    signal(SIGCHLD, sync_on_sigchld);   /* reap the Host child -> no zombies */
#endif
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
