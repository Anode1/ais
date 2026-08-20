/* serve.c -- `ais serve`: an OPTIONAL built-in web GUI. See serve.h.
 *
 * A GUI WRAPPER, not the program: the index, the store and the algorithms live in
 * ais.c, store.c, merge.c, post.c, compact.c. This file only lets a browser drive
 * that engine, and embeds a small web page as a C string (PAGE, below) so the binary
 * stays self-contained -- that blob is HTML/JS, NOT C, and not a sample of how AIS
 * is written. It is the only GUI file under c/. A single-threaded HTTP/1.0 loop on
 * 127.0.0.1 serves the page plus two endpoints that call the engine directly: no
 * Python, no framework, no DB. A SKETCH: localhost only, one client at a time, the
 * request must fit a single read.
 */
#define _DEFAULT_SOURCE          /* htonl, strtok_r */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>       /* strncasecmp: case-insensitive HTTP header match */
#ifndef _WIN32
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>      /* struct timeval, for the per-client recv timeout */
#include <unistd.h>
#endif

#include "ais.h"
#include "b64.h"           /* base64 a document blob's content onto the line-based wire */
#include "common.h"
#include "doc.h"
#include "secret.h"      /* GUI encrypt: secret_encrypt for the "aisc:" marker */
#include "stats.h"       /* ais_stats: the GUI shows what clean-up would reclaim */
#include "locate.h"       /* ais_default_set: persist the chosen store */
#include "win.h"          /* Winsock + socket shims on native Windows; empty on POSIX */
#include "serve.h"

/* LAN sync (the GUI's Host/Join, mirroring the mobile Sync feature): available only
 * where the sync transport is -- POSIX plus the vendored crypto module, the same
 * guard sync.c uses. Elsewhere the routes report that the build lacks it. */
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
 * The UI, embedded as a string so the binary is self-contained; the
 * one-string-literal-per-line shape is not the project's C style. To change the UI,
 * edit the PAGE string below -- it is the only copy. Vanilla JS; the API is
 * form-encoded keys + a plain-text body/reply (no JSON, and a text/plain POST is a
 * "simple" request, so no CORS preflight). */
static const char PAGE[] =
"<!doctype html><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<meta name=theme-color content=#4f5b92><title>AIS</title>"
"<style>"
/* The palette mirrors the Flutter app (Material 3, indigo seed), so the two
 * front ends read as one product. */
":root{--accent:#4f5b92;--fabbg:#dee0ff;--fabfg:#3b4472;--line:#e3e3ea;--muted:#54545e;--bg:#efeff6;--fg:#14141a;--card:#fff;--field:#fafafc;--barbg:rgba(255,255,255,.62);--danger:#a3281c}"
"@media(prefers-color-scheme:dark){:root{--accent:#bbc3ff;--fabbg:#414a71;--fabfg:#dee0ff;--line:#33333e;--muted:#9b9ba7;--bg:#15151b;--fg:#e7e7ef;--card:#23232c;--field:#2b2b35;--barbg:rgba(28,28,36,.72);--danger:#ff8172}}"
"*{box-sizing:border-box}html{color-scheme:light dark}"   /* native date/checkbox/scrollbars follow the theme */
"body{font:16px/1.45 system-ui,sans-serif;color:var(--fg);background:var(--bg);margin:0}"
"#bar{position:sticky;top:0;z-index:5;padding:12px 16px;background:var(--barbg);"
"backdrop-filter:blur(14px);-webkit-backdrop-filter:blur(14px);border-bottom:1px solid var(--line)}"
".titlerow{display:flex;align-items:baseline;gap:.5rem}"
".brand{font-size:1.55rem;font-weight:700}.muted{color:var(--muted)}"
"#count{margin-left:auto;font-size:.8rem}"
".searchrow{display:flex;align-items:center;margin-top:.6rem;background:var(--card);"
"border:1px solid var(--line);border-radius:28px;padding:0 .8rem}"
"#q{flex:1;font:inherit;border:0;outline:none;background:transparent;padding:.85rem .4rem}"
"#out{max-width:720px;margin:0 auto;padding:.5rem 1rem 7rem}"
".hit{position:relative;padding:.85rem 2.8rem .85rem .2rem;border-bottom:1px solid var(--line);word-break:break-word}"
".hit:last-child{border-bottom:0}.hit a{color:var(--accent);text-decoration:underline}"
".empty{color:var(--muted);text-align:center;margin-top:3rem}"
".storerow{font-size:.72rem;margin-top:.45rem;display:flex;flex-wrap:wrap;gap:.4rem;align-items:center}"
".storerow .link{white-space:nowrap}"
"#store{flex:1 1 100%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
".link{border:0;background:none;color:var(--accent);cursor:pointer;font:inherit;text-decoration:underline;padding:0}"
".fab{position:fixed;right:18px;bottom:78px;border:0;border-radius:18px;padding:.9rem 1.3rem;"
"cursor:pointer;font:inherit;font-weight:600;color:var(--fabfg);background:var(--fabbg);box-shadow:0 3px 10px rgba(0,0,0,.18)}"
"#bnav{position:fixed;left:0;right:0;bottom:0;z-index:6;display:flex;background:var(--card);border-top:1px solid var(--line);box-shadow:0 -2px 12px rgba(0,0,0,.05)}"
"#bnav button{flex:1;border:0;background:none;padding:.5rem 0 .62rem;font:inherit;font-size:.75rem;color:var(--muted);cursor:pointer;display:flex;flex-direction:column;align-items:center;gap:.12rem}"
"#bnav button .ic{font-size:1.3rem;line-height:1;padding:.15rem .85rem;border-radius:14px}"
"#bnav button.on{color:var(--accent)}#bnav button.on .ic{background:var(--fabbg);color:var(--fabfg)}"
"#sheet,#syncsheet,#editsheet,#dsheet{position:fixed;inset:0;z-index:10;background:rgba(0,0,0,.35);display:flex;align-items:center;justify-content:center}"
"#sheet[hidden],#syncsheet[hidden],#editsheet[hidden],#dsheet[hidden]{display:none}"
".card{width:100%;max-width:560px;background:var(--card);border-radius:18px;padding:1.2rem;margin:1rem}"
".card h2{margin:0 0 1rem;font-size:1.15rem}"
".card textarea,.card input{width:100%;font:inherit;padding:.7rem .8rem;border:1px solid var(--line);"
"border-radius:10px;margin-bottom:.8rem;background:var(--field)}"
".actions{display:flex;justify-content:flex-end;gap:.6rem}"
".actions button{font:inherit;padding:.6rem 1.1rem;border-radius:10px;cursor:pointer}"
".ghost{border:1px solid var(--line);background:var(--card)}"
/* Self-contained: the empty-state CTA stands OUTSIDE any .actions row, and
 * inheriting nothing it rendered as a cramped chip -- the first button a new
 * user saw. Inside .actions the same values apply twice, harmlessly. */
".primary{border:0;background:var(--accent);color:#fff;font:inherit;font-weight:600;padding:.6rem 1.1rem;border-radius:10px;cursor:pointer}"
".getbtn{border:0;background:var(--accent);color:#fff;font:inherit;font-weight:600;"
"padding:.7rem 1.4rem;border-radius:22px;cursor:pointer;white-space:nowrap}"
".minibtn{border:1px solid var(--line);background:var(--card);color:var(--muted);font:inherit;"
"font-size:.78rem;padding:.28rem .8rem;border-radius:8px;cursor:pointer}"
".minibtn.active{background:var(--accent);color:#fff;border-color:var(--accent)}"
".daygroup{font-size:.95rem;color:var(--fg);font-weight:700;margin:1.1rem 0 .2rem}"
".meta{font-size:.8rem;color:var(--muted);margin-top:.2rem}"
"#tlfrom,#tlto{border:1px solid var(--line);background:var(--card);color:var(--fg);border-radius:10px;padding:.3rem .5rem;font-size:.85rem}"
".tagrow{display:flex;align-items:center;justify-content:space-between;padding:.55rem .2rem .1rem}"
".taglink{border:0;background:none;color:var(--accent);font:inherit;cursor:pointer;padding:0;text-align:left;word-break:break-word}"
".tagcount{font-size:.8rem;color:var(--muted);background:var(--card);border:1px solid var(--line);border-radius:10px;padding:.1rem .5rem;min-width:2rem;text-align:center}"
".act{position:absolute;right:0;top:50%;transform:translateY(-50%);display:flex;flex-direction:row-reverse;align-items:center;gap:.4rem}"
".actmenu{display:inline-flex;gap:.4rem;background:var(--bg)}.actmenu[hidden]{display:none}"
".actbtn{border:0;background:none;color:var(--muted);font:inherit;font-size:.8rem;cursor:pointer;padding:0;min-width:44px;min-height:44px;display:inline-flex;align-items:center;justify-content:center}"
".actbtn:hover{color:var(--accent)}"
".actbtn.del:hover{color:var(--danger)}"
".tagacts{display:flex;justify-content:space-between;align-items:center;gap:3rem;padding:0 .2rem .3rem;border-bottom:1px solid var(--line)}"
".tagacts .actbtn{min-height:44px;display:inline-flex;align-items:center;touch-action:manipulation}"
".actbtn.danger{color:var(--muted)}.actbtn.danger:hover{color:var(--danger)}"
":focus-visible{outline:2px solid var(--accent);outline-offset:2px}"
".actbtn.danger:focus-visible{outline-color:var(--danger)}"
"#dsheet .prev{list-style:none;margin:.2rem 0 .6rem;padding:0;max-height:38vh;overflow-y:auto}"
"#dsheet .prev li{font-size:.8rem;color:var(--muted);padding:.15rem 0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
"#dsheet .lead{color:var(--danger);font-weight:600;margin:.2rem 0}"
"#dsheet .card{max-height:85vh;overflow-y:auto}"
"#dsheet input{font-size:16px}"
"#dsheet .card:focus{outline:none}"
"#dsheet .ghost{font:inherit;padding:.6rem 1rem;border-radius:8px;cursor:pointer;color:var(--fg)}"
".danger-btn{background:var(--danger);color:#fff;border:0;border-radius:8px;font:inherit;font-weight:600;padding:.6rem 1rem;cursor:pointer}"
".danger-btn[disabled]{opacity:.45;cursor:not-allowed}"
".loadmore{display:block;width:100%;margin:1rem 0;padding:.6rem;border:1px solid var(--line);background:var(--card);color:var(--accent);border-radius:8px;cursor:pointer;font:inherit}"
".loadmore:hover{background:var(--field)}"
".chips{display:flex;flex-wrap:wrap;gap:.4rem;margin:.2rem 0 .7rem}"
".chip{display:inline-flex;align-items:center;gap:.35rem;background:var(--field);border:1px solid var(--line);border-radius:16px;padding:.25rem .7rem;font-size:.9rem}"
".chip button{border:0;background:none;color:var(--muted);cursor:pointer;font:inherit;font-size:1rem;line-height:1;padding:0}"
".chip button:hover{color:var(--danger)}"
"#toast{position:fixed;left:50%;bottom:82px;transform:translateX(-50%);z-index:20;display:flex;align-items:center;gap:1rem;background:#2b2b33;color:#fff;padding:.65rem 1.1rem;border-radius:10px;box-shadow:0 4px 16px rgba(0,0,0,.35)}"
"#toast[hidden]{display:none}#toast button{color:#bbc3ff;border:0;background:none;text-decoration:underline;cursor:pointer;font:inherit}"
"@media(max-width:600px){#sheet,#editsheet,#syncsheet{align-items:flex-end}"
"#sheet .card,#editsheet .card,#syncsheet .card{margin:0;max-width:none;border-radius:22px 22px 0 0;padding-bottom:1.4rem}"
"#sheet .actions .primary{flex:1;border-radius:24px;padding:.8rem 1.1rem}}"
"</style>"
"<header id=bar><div class=titlerow><span class=brand>AIS</span><span id=count class=muted></span></div>"
"<div class=searchrow><input id=q type=search placeholder='type tags to filter' autocomplete=off autofocus>"
"<button id=seg-recall class=getbtn>Search</button></div>"
"<label class=allk style='display:flex;align-items:center;gap:.4rem;font-size:.85rem;color:var(--muted);margin-top:.5rem'><input id=anyk type=checkbox style='width:auto'> Match any tag</label>"
"<div class=storerow><span id=store class=muted></span><span style='flex:1'></span>"
"<button id=cleanbtn class=link>clean up</button>""<button id=syncbtn class=link>sync</button><button id=storebtn class=link>change</button></div>"
"<div id=storeedit class=storerow style='display:none;margin-top:.4rem'><input id=storepath placeholder='Library folder (full path)' autocomplete=off style='flex:1;font:inherit'><button id=storeok class=link>open</button><button id=storecancel class=link>cancel</button></div>"
"<div id=tlrange style='display:none;gap:.4rem;align-items:center;margin-top:.5rem;font-size:.85rem;color:var(--muted)'>"
"<span>from</span><input id=tlfrom type=date style='font:inherit'>"
"<span>to</span><input id=tlto type=date style='font:inherit'>"
"<button id=tlclear class=link>clear</button></div></header>"
"<main id=out><p class=empty>Loading...</p></main>"
"<button id=addbtn class=fab>+ Add</button>"
"<div id=toast hidden><span>Deleted</span><button id=toastundo>Undo</button></div>"
"<nav id=bnav><button data-v=recall><span class=ic>&#128269;</span>Search</button>"
"<button data-v=timeline class=on><span class=ic>&#128336;</span>Recent</button>"
"<button data-v=tags><span class=ic>&#127991;</span>Tags</button></nav>"
"<div id=dsheet hidden role=dialog aria-modal=true aria-labelledby=dstitle>""<div class=card tabindex=-1><h2 id=dstitle>Delete records?</h2>""<p class=lead>This deletes the records, not the tag.</p>""<p id=dsbody class=muted style='font-size:.9rem'></p>""<ul id=dsprev class=prev></ul>""<p><button id=dskeep class=link>Keep the records: remove just the tag</button></p>""<label for=dsname style='font-size:.85rem;color:var(--muted)'>Type the tag name to confirm</label>""<input id=dsname autocomplete=off autocapitalize=off autocorrect=off spellcheck=false>""<div class=row style='display:flex;gap:.5rem;justify-content:flex-end;margin-top:.6rem'>""<button id=dscancel class=ghost>Cancel</button>""<button id=dsgo class=danger-btn disabled>Delete</button></div></div></div>""<div id=sheet hidden><div class=card><h2>Add to your memory</h2>"
"<input id=vk placeholder='Tags (space or comma separated, optional)'>"
"<textarea id=v rows=3 placeholder='What to remember: a link, a note, a number...'></textarea>"
"<div class=encrow style='display:flex;align-items:center;gap:.5rem;margin:.1rem 0'>"
"<label style='display:flex;align-items:center;gap:.35rem;font-size:.85rem;color:var(--muted);white-space:nowrap'>"
"<input id=enc type=checkbox style='width:auto'> Encrypt</label>"
"<input id=pp type=password placeholder='Passphrase' hidden style='flex:1'></div>"
"<div class=actions><button id=cancel class=ghost>Cancel</button><button id=save class=primary>Save</button></div>"
"</div></div>"
/* Edit modal: in-place value edit + a chip tag editor (Flutter parity). The value
 * box is hidden for encrypted/blob rows. */
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
"<div class=actions style='justify-content:center'><button id=impbtn class=ghost>Import</button><button id=expbtn class=primary>Export</button></div>"
/* Auto-sync a shared folder (Syncthing / a cloud folder). One tap runs a pass; the
 * path is remembered and a pass also runs on load and after each change. */
"<p class=muted style='margin:1rem 0 .4rem;font-size:.85rem'>Or auto-sync a shared folder. Best with Syncthing; a versioning cloud may keep deleted items:</p>"
"<div style='display:flex;gap:.5rem'><input id=syncfld placeholder='/path/to/shared/folder' autocomplete=off style='flex:1'>"
"<button id=syncfldbtn class=getbtn>Sync</button></div>"
"<p id=syncfldmsg class=muted style='margin:.4rem 0 0;font-size:.8rem'></p>"
"<button id=syncfldany class=getbtn style='display:none;margin-top:.4rem'>Sync with it anyway</button></div>"
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
/* `gen` counts view switches. Every loader below appends to #out AFTER an await, so
 * a response landing once the user has moved on would repaint the old view's rows
 * over the new. Each loader captures gen before its fetch and drops a stale result. */
"var view='recall',viewGen=0;"
/* empty-state call-to-action, reused by every empty view */
"var addCTA='<button class=primary style=\"margin-top:1rem\" onclick=openSheet()>+ Add</button>';"
/* accept a comma as an optional tag separator; collapse extra whitespace, so
 * \"home, wifi\" and \"home   wifi\" both mean the tags home + wifi (Flutter parity) */
"function normkeys(s){return s.replace(/,/g,' ').trim().replace(/\\s+/g,' ')}"
"var tlBefore=0,tlDay=null,tlN=0,tlPage=100;"   /* timeline keyset paging state */
"var rcAfter=0,rcN=0,rcMore=false,rcQ='',rcOr='',rcT0=0;"   /* recall keyset paging */
"var tgAfterc=0,tgAfterk='',tgN=0,tgMore=false;"           /* tags keyset paging */
"var loadingMore=false;"                        /* one page fetch at a time (infinite scroll) */
/* fillVal: append V to NODE, turning every embedded http(s) URL into a link, not
 * only values that are wholly a URL ("Title - https://..." keeps the title as text). */
"function fillVal(node,v){var re=/https?:\\/\\/[^\\s]+/g,last=0,m;"
"while((m=re.exec(v))!==null){"
"if(m.index>last)node.appendChild(document.createTextNode(v.slice(last,m.index)));"
"var a=document.createElement('a');a.href=m[0];a.textContent=m[0];"
"a.target='_blank';a.rel='noopener';node.appendChild(a);last=m.index+m[0].length}"
"if(last<v.length)node.appendChild(document.createTextNode(v.slice(last)))}"
/* An "aisc:" value shows as an opaque lock + a Reveal button; revealing prompts for
 * the passphrase and decrypts via /api/reveal (passphrase in the POST body, never
 * the URL). The cleartext is shown until the next render. */
"function fillSecret(node,v){var s=document.createElement('span');s.textContent='\\uD83D\\uDD12 encrypted ';s.style.color='var(--muted)';"
"var b=document.createElement('button');b.className='actbtn';b.textContent='Reveal';"
"b.onclick=function(){revealSecret(node,v)};node.appendChild(s);node.appendChild(b)}"
/* inline password field, not prompt(): prompt() is silently disabled in an
 * installed PWA / app window. */
"async function revealSecret(node,v){node.innerHTML='';"
"var i=document.createElement('input');i.type='password';i.placeholder='Passphrase';i.autocomplete='off';i.style.cssText='font:inherit;padding:.2rem .4rem';"
"var b=document.createElement('button');b.className='actbtn';b.textContent='Show';b.style.marginLeft='.4rem';"
"var x=document.createElement('button');x.className='actbtn';x.textContent='cancel';x.style.marginLeft='.4rem';x.onclick=function(){node.innerHTML='';fillSecret(node,v)};"
"async function go(){var pp=i.value;if(!pp)return;"
"var r=await fetch('/api/reveal',{method:'POST',body:pp+'\\n'+v});var t=await r.text();"
"node.innerHTML='';if(t){fillVal(node,t);var c=document.createElement('button');c.className='actbtn';c.textContent='copy';c.style.marginLeft='.5rem';c.onclick=function(){copyText(t,c)};node.appendChild(c)}else{node.textContent='(cannot decrypt)'}}"
"b.onclick=go;i.onkeydown=function(e){if(e.key==='Enter')go();else if(e.key==='Escape'){node.innerHTML='';fillSecret(node,v)}};"
"node.appendChild(i);node.appendChild(b);node.appendChild(x);i.focus()}"
/* A document blob comes over the wire as "aisdoc:<base64 of the content>", so the
 * (possibly multi-line) content survives the line-based record split. Decode as
 * UTF-8 bytes and show it verbatim (newlines preserved). */
"function docText(v){try{var s=atob(v.slice(7)),b=new Uint8Array(s.length),i;for(i=0;i<s.length;i++)b[i]=s.charCodeAt(i);return new TextDecoder().decode(b)}catch(e){return v}}"
"function fillDoc(node,v){var d=document.createElement('div');d.style.whiteSpace='pre-wrap';d.textContent=docText(v);node.appendChild(d)}"
/* 'not on this device' (Flutter parity): a present doc blob arrives as
 * aisdoc:<content>; an absent one falls through as the raw 'blobs/' path, as does a
 * file:// or absolute path, neither reachable from a browser. Badge those. */
"function notHere(v){var p=v;if(p.indexOf('aisc:@')==0)p=p.slice(6);"
"if(p.indexOf('blobs/')==0)return true;"
"if(p.indexOf('file://')==0)return true;"
"if(p.charAt(0)=='/')return true;return false}"
"function awayBadge(){var s=document.createElement('span');s.textContent=' \\u2601';"
"s.title='Not on this device: open it on the desktop, or mount that disk.';"
"s.style.color='var(--muted)';s.style.marginLeft='.3rem';return s}"
/* Recall is keyset-paged like the timeline: `more` appends the next page (id >
 * rcAfter) instead of reloading. Recall emits ascending, so rcAfter tracks the
 * largest id shown. */
"async function recall(more){var o=$('out');var qq=normkeys($('q').value);var g=viewGen;"
"if(!more){if(!qq)return;rcAfter=0;rcN=0;rcQ=qq;rcOr=($('anyk')&&$('anyk').checked)?'&or=1':'';rcT0=performance.now();o.className='';o.innerHTML=''}"
"if(!rcQ)return;"
"var u='/api/get?keys='+encodeURIComponent(rcQ)+rcOr+'&meta=1&count='+tlPage+(rcAfter>0?'&after='+rcAfter:'');"
"var L=(await(await fetch(u)).text()).split('\\n').filter(function(s){return s.length});"
"if(g!=viewGen)return;"                                  /* the view moved on: drop it */
"var mb=$('rcmore');if(mb)mb.remove();"
"if(!rcN&&!L.length){o.textContent='No results for '+rcQ;o.className='empty';$('count').textContent='0 results';rcMore=false;return}o.className='';"
/* meta=1 lines are id|keys|value; showing WHY a row matched (its tags) is what
 * makes an OR result readable. */
"L.forEach(function(ln){var p=ln.indexOf('|'),q=ln.indexOf('|',p+1),id=p>=0?ln.slice(0,p):'',ks=q>=0?ln.slice(p+1,q):'',v=q>=0?ln.slice(q+1):ln.slice(p+1);"
"var r=document.createElement('div');r.className='hit';if(id)r.dataset.id=id;"
"(v.indexOf('aisc:')==0?fillSecret(r,v):v.indexOf('aisdoc:')==0?fillDoc(r,v):fillVal(r,v));if(notHere(v))r.appendChild(awayBadge());"
"var km=document.createElement('div');km.className='meta';km.textContent=ks||'(no tags)';r.appendChild(km);"
"if(id){r.appendChild(rowActions(id,v));rcAfter=+id}o.appendChild(r)});"
"rcN+=L.length;$('count').textContent=rcN+' result'+(rcN==1?'':'s')+' - '+(performance.now()-rcT0).toFixed(0)+' ms';"
"if(L.length==tlPage){rcMore=true;var b=document.createElement('button');b.id='rcmore';b.className='loadmore';b.textContent='Load more';b.onclick=pageMore;o.appendChild(b)}else rcMore=false}"
/* per-row edit (attach/detach keys by id) and delete; both refresh the view */
"function rowActions(id,v){var d=document.createElement('div');d.className='act';"
"var m=document.createElement('button');m.className='actbtn';m.textContent='\\u22EE';m.title='more';m.style.fontSize='1.2rem';"
"var box=document.createElement('span');box.className='actmenu';box.hidden=true;"
"if(v.indexOf('aisc:')!=0){var c=document.createElement('button');c.className='actbtn';c.textContent='copy';c.onclick=function(){copyText(v.indexOf('aisdoc:')==0?docText(v):v,c)};box.appendChild(c)}"
"var e=document.createElement('button');e.className='actbtn';e.textContent='edit';e.onclick=function(){openEdit(id,v)};"
"var x=document.createElement('button');x.className='actbtn del';x.textContent='\\u2715 delete';x.onclick=function(){delRec(id)};"
"box.appendChild(e);box.appendChild(x);m.onclick=function(){box.hidden=!box.hidden};"
"d.appendChild(m);d.appendChild(box);return d}"
"async function copyText(t,btn){try{await navigator.clipboard.writeText(t);if(btn){var o=btn.textContent;btn.textContent='copied';setTimeout(function(){btn.textContent=o},1200)}}catch(e){alert('copy needs https or localhost')}}"
/* Deferred delete + Undo (Flutter parity): hide the row and start a 5s window. The
 * engine del() only fires when the window lapses OR another action flushes it --
 * never on Undo, which just re-shows the row (nothing was deleted). */
"var delTimer=null,delRow=null,delCommit=null,delUndoFn=null;"
"function hideToast(){$('toast').hidden=true}"
"function toastClaim(){if(syncFldTimer){clearTimeout(syncFldTimer);syncFldTimer=null}$('toastundo').hidden=false}"
"function delFlush(){if(delTimer){clearTimeout(delTimer);delTimer=null}if(delCommit){var f=delCommit;delCommit=null;delRow=null;delUndoFn=null;f();if(syncFolderSaved())syncFolderRun(true)}hideToast()}"
"function delRec(id){delFlush();toastClaim();$('toast').firstChild.textContent='Deleted';"                       /* commit any prior pending delete first */
"var row=document.querySelector('.hit[data-id=\"'+id+'\"]');if(row)row.style.display='none';delRow=row;"
"delCommit=function(){fetch('/api/del?id='+id,{method:'POST'})};"
"$('toast').hidden=false;delTimer=setTimeout(delFlush,5000)}"
"function delUndo(){if(delTimer){clearTimeout(delTimer);delTimer=null}if(delRow)delRow.style.display='';"
"if(delUndoFn){delUndoFn();delUndoFn=null}delRow=null;delCommit=null;hideToast()}"
/* Edit modal: value (when editable) + a chip tag editor, computing a minimal
 * +tag/-tag delta on save. */
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
"async function loadTimeline(more){var o=$('out');var g=viewGen;"
"var f=$('tlfrom')?$('tlfrom').value:'',tt=$('tlto')?$('tlto').value:'';"
"if(!more){tlBefore=0;tlDay=null;tlN=0;o.className='';o.innerHTML=''}"
"var u='/api/timeline?count='+tlPage+(tlBefore>0?'&before='+tlBefore:'')+(f?'&from='+f:'')+(tt?'&to='+tt:'');"
"var L=(await(await fetch(u)).text()).split('\\n').filter(function(s){return s.length});"
"if(g!=viewGen)return;"                                  /* the view moved on: drop it */
"var mb=$('tlmore');if(mb)mb.remove();"
"if(!tlN&&!L.length){o.innerHTML='<div class=empty><p>Nothing saved yet.</p>'+addCTA+'</div>';return}"
"L.forEach(function(ln){var r=parseTL(ln),lt=locDT(r.ts),d=lt?lt.day:'';"
"if(d!==tlDay){tlDay=d;var h=document.createElement('div');h.className='daygroup';"
"h.textContent=d?fmtDay(d):'(undated)';o.appendChild(h)}"
"var row=document.createElement('div');row.className='hit';if(r.id)row.dataset.id=r.id;(r.value.indexOf('aisc:')==0?fillSecret(row,r.value):r.value.indexOf('aisdoc:')==0?fillDoc(row,r.value):fillVal(row,r.value));if(notHere(r.value))row.appendChild(awayBadge());"
"var m=document.createElement('div');m.className='meta';"
"var tm=lt&&lt.time?lt.time+' \\u00b7 ':'';"
"m.textContent=tm+(r.keys||'(no tags)');row.appendChild(m);"
"if(r.id){row.appendChild(rowActions(r.id,r.value));tlBefore=r.id}o.appendChild(row)});"
"tlN+=L.length;$('count').textContent=tlN+' record'+(tlN==1?'':'s');"
"if(L.length==tlPage){var b=document.createElement('button');b.id='tlmore';b.className='loadmore';"
"b.textContent='Load more';b.onclick=pageMore;o.appendChild(b)}}"
/* Tags keyset-paged too: the cursor is the (count, key) of the last row, in the
 * busiest-first order the engine emits; `more` appends the next slice. */
"async function loadTags(more){var o=$('out');var g=viewGen;"
"if(!more){tgAfterc=0;tgAfterk='';tgN=0;o.className='';o.innerHTML=''}"
"var u='/api/tags?count='+tlPage+(tgAfterk?'&afterc='+tgAfterc+'&afterk='+encodeURIComponent(tgAfterk):'');"
"var L=(await(await fetch(u)).text()).split('\\n').filter(function(s){return s.length});"
"if(g!=viewGen)return;"                                  /* the view moved on: drop it */
"var mb=$('tgmore');if(mb)mb.remove();"
"if(!tgN&&!L.length){o.innerHTML='<p class=empty>No tags yet.</p>';$('count').textContent='';tgMore=false;return}"
"L.forEach(function(ln){var p=ln.indexOf('|'),c=ln.slice(0,p),k=ln.slice(p+1);"
"var row=document.createElement('div');row.className='tagrow';"
"var b=document.createElement('button');b.className='taglink';b.textContent=k;"
"b.onclick=function(){$('q').value=k;setView('recall')};"
"var n=document.createElement('span');n.className='tagcount';n.textContent=c;"
"row.appendChild(b);row.appendChild(n);o.appendChild(row);"
"var ac=document.createElement('div');ac.className='tagacts';"
"var u=document.createElement('button');u.className='actbtn';u.textContent='Remove tag';"
"u.onclick=function(){untagKey(k)};"
"var g=document.createElement('button');g.className='actbtn danger';"
"g.textContent='Delete '+c+' record'+(+c==1?'':'s');"
"g.onclick=function(){openDelUnder(k)};"
"ac.appendChild(u);ac.appendChild(g);o.appendChild(ac);tgAfterc=+c;tgAfterk=k});"
"tgN+=L.length;$('count').textContent=tgN+' tag'+(tgN==1?'':'s');"
"if(L.length==tlPage){tgMore=true;var mo=document.createElement('button');mo.id='tgmore';mo.className='loadmore';mo.textContent='Load more';mo.onclick=pageMore;o.appendChild(mo)}else tgMore=false}"
/* The two tag-level operations, opposite in consequence: untag reuses the record
 * delete's deferred-commit + Undo window (nothing has been sent to the engine yet,
 * so the Undo really does undo); delete-under gets a modal with a preview, an escape hatch to untag, and
 * type-to-confirm, because it destroys records that are NOT on this screen -- each
 * also disappears from every other tag it is filed under. */
/* Finds its own row, so both front ends expose the same untagKey(key,count). */
"function untagKey(k){delFlush();toastClaim();"
"var row=null,acts=null;"
"[].forEach.call(document.querySelectorAll('.taglink'),function(b){"
"if(b.textContent==k){row=b.parentNode;acts=row.nextSibling}});"
"if(row)row.style.display='none';if(acts)acts.style.display='none';delRow=null;"
"$('toast').firstChild.textContent=\"Removed tag '\"+k+\"'\";"
"delCommit=function(){fetch('/api/untag?keys='+encodeURIComponent(k),{method:'POST'}).then(function(){if(view=='tags')loadTags()})};"
"delUndoFn=function(){if(row)row.style.display='';if(acts)acts.style.display=''};"
"$('toast').hidden=false;delTimer=setTimeout(delFlush,5000)}"
/* Recompute the count and the preview at OPEN time: the row badge can be stale,
 * and a number in a destructive confirmation has to be the number that will go. */
"var dsKey='';"
"async function openDelUnder(k){delFlush();"
"var t=await(await fetch('/api/get?keys='+encodeURIComponent(k))).text();"
"var L=t.split('\\n').filter(function(s){return s.length});"
/* /api/get emits one line per LINK, and a record may hold several, so counting
 * lines over-counts records. Group by id: the number in the destructive copy must
 * be the number that will go. */
"var R=[],seen={};L.forEach(function(ln){var b=ln.indexOf('|');"
"var id=b<0?ln:ln.slice(0,b),v=b<0?ln:ln.slice(b+1);"
"if(seen[id]===undefined){seen[id]=R.length;R.push({v:v,extra:0})}else R[seen[id]].extra++});"
"if(!R.length){$('count').textContent=\"Nothing is filed under '\"+k+\"'\";loadTags();return}"
"dsKey=k;var n=R.length,w=n+' record'+(n==1?'':'s');"
"$('dstitle').textContent='Delete '+w+'?';"
"$('dsbody').textContent='All '+w+\" filed under '\"+k+\"' are deleted, and each disappears from every other tag it is filed under too.\";"
"var ul=$('dsprev');ul.innerHTML='';"
"R.slice(0,10).forEach(function(r){var li=document.createElement('li');"
"li.textContent=r.v+(r.extra?' (+'+r.extra+' more link'+(r.extra==1?'':'s')+' on this record)':'');"
"ul.appendChild(li)});"
"if(n>10){var li=document.createElement('li');li.textContent='... and '+(n-10)+' more. To see them all, tap the tag.';ul.appendChild(li)}"
"$('dskeep').textContent=\"Keep the records: remove just the tag\";"
"$('dsname').value='';$('dsname').placeholder=k;"
"$('dsgo').textContent='Delete '+w;$('dsgo').disabled=true;"
"$('dsheet').hidden=false;$('dsheet').querySelector('.card').focus()}"
"function closeDel(){$('dsheet').hidden=true;dsKey=''}"
/* Infinite scroll: near the bottom, pull the next page for whatever view is up.
 * loadingMore serializes fetches so a scroll burst can't double-load a page. */
"function pageMore(){if(loadingMore)return;loadingMore=true;"
"var p=view=='timeline'?loadTimeline(true):view=='recall'?recall(true):loadTags(true);"
"Promise.resolve(p).catch(function(){}).then(function(){loadingMore=false})}"
"window.addEventListener('scroll',function(){"
"if(window.innerHeight+window.scrollY<document.body.scrollHeight-600)return;"
"if(document.querySelector('.loadmore'))pageMore()});"
"function setView(v){delFlush();view=v;viewGen++;"
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
 * retry: never a stuck, silent modal. */
"try{var r=await fetch('/api/put?keys='+encodeURIComponent(k)+(enc?'&enc=1':''),{method:'POST',body:enc?(pp+'\\n'+v):v});"
"if(!r.ok)throw new Error('server '+r.status)}catch(e){alert('Save failed ('+e.message+'). Nothing was saved.');return}"
"closeSheet();setView('timeline');if(syncFolderSaved())syncFolderRun(true)}"
"$('q').addEventListener('keydown',function(e){if(e.key=='Enter')setView('recall')});"
"$('seg-recall').onclick=function(){setView('recall')};"
"[].forEach.call(document.querySelectorAll('#bnav button'),function(b){b.onclick=function(){setView(b.dataset.v)}});"
"$('tlfrom').onchange=function(){loadTimeline()};$('tlto').onchange=function(){loadTimeline()};"
"$('tlclear').onclick=function(){$('tlfrom').value='';$('tlto').value='';loadTimeline()};"
"$('addbtn').onclick=openSheet;$('cancel').onclick=closeSheet;$('save').onclick=save;"
"$('toastundo').onclick=delUndo;"
"$('dscancel').onclick=closeDel;"
"$('dsheet').onclick=function(e){if(e.target===$('dsheet'))closeDel()};"
"$('dskeep').onclick=function(){var k=dsKey;closeDel();untagKey(k)};"
"$('dsname').oninput=function(){$('dsgo').disabled=this.value.trim()!==dsKey};"
"$('dsgo').onclick=async function(){if(this.disabled)return;var k=dsKey;closeDel();"
"var r=await(await fetch('/api/del-under?keys='+encodeURIComponent(k),{method:'POST'})).text();"
"var m=+(r.match(/\\d+/)||[0])[0];$('count').textContent='Deleted '+m+' record'+(m==1?'':'s');"
"loadTags()};"
"document.addEventListener('keydown',function(e){if(e.key=='Escape'&&!$('dsheet').hidden)closeDel()});"
"$('edcancel').onclick=function(){$('editsheet').hidden=true};$('edsave').onclick=edSave;"
"$('edtag').addEventListener('keydown',function(e){if(e.key=='Enter'||e.key==','){e.preventDefault();edAdd()}});"
"$('edtag').addEventListener('blur',edAdd);"
"$('editsheet').addEventListener('click',function(e){if(e.target==$('editsheet'))$('editsheet').hidden=true});"
"$('enc').onchange=function(){$('pp').hidden=!$('enc').checked;if($('enc').checked)$('pp').focus()};"
"$('sheet').addEventListener('click',function(e){if(e.target==$('sheet'))closeSheet()});"
"async function loadStore(){$('store').textContent='Library: '+await(await fetch('/api/where')).text()}"
/* inline field, not prompt(): prompt() is silently disabled in installed-PWA and
 * app windows. */
"async function changeStore(){var cur=await(await fetch('/api/where')).text();"
"$('storepath').value=cur;$('storeedit').style.display='flex';$('storepath').focus()}"
"async function storeApply(){var d=$('storepath').value.trim();$('storeedit').style.display='none';if(!d)return;"
"var r=await fetch('/api/store',{method:'POST',body:d});"
"if(r.ok){$('q').value='';$('out').innerHTML='';$('count').textContent='';loadStore()}"
"else{alert('Could not open that index')}}"
/* Reclaim deleted records: with no CLI on a phone the store would grow forever and
 * deleted records' tags would linger in the index. The second, privacy question is
 * asked separately: forgetting is the one choice here another device can undo. */
"async function cleanUp(){"
"var st=await(await fetch('/api/stats')).text();"
"var d=(st.match(/deleted:\\s*(\\d+)/)||[0,'0'])[1];"
"if(d=='0'){$('count').textContent='Nothing to clean up';return}"
"if(!confirm('Reclaim the space of '+d+' deleted record'+(d=='1'?'':'s')+'?'))return;"
"var f=confirm('Also FORGET what was deleted?\\n\\nThey stay deleted here, but this device can no longer tell your other devices about them, and nothing is left for anyone to test a guess against.\\n\\nSync your other devices FIRST -- one that has not seen these deletions can send them back.\\n\\nOK = forget.  Cancel = just reclaim the space.');"
"var r=await(await fetch('/api/compact'+(f?'?forget=1':''),{method:'POST'})).text();"
"$('count').textContent=r.trim()=='cleaned and forgotten'?'Cleaned up, deletions forgotten':'Cleaned up';"
"setView(view)}"
"$('cleanbtn').onclick=cleanUp;"
"$('storebtn').onclick=changeStore;$('storeok').onclick=storeApply;"
"$('storecancel').onclick=function(){$('storeedit').style.display='none'};"
"$('storepath').onkeydown=function(e){if(e.key==='Enter')storeApply();else if(e.key==='Escape')$('storeedit').style.display='none'};"
"loadStore();"
/* #tags / #recall / #timeline opens that view directly, so a view is linkable
 * (and reachable by a screenshot or a UI test, which cannot click before load). */
"var h=(location.hash||'').slice(1);"
"setView(h=='tags'||h=='recall'||h=='timeline'?h:'timeline');"   /* else open on content, not a blank search */
/* ---- Sync (Host / Join), mirroring the mobile Sync feature ---------------
 * qrGen: a small pure-JS byte-mode QR encoder (ECC level L, mask 0, versions 1-5)
 * so a phone can scan the url+token with no server-side or network dependency.
 * Returns a matrix of 0/1 modules. Capped at v5 (<=106 bytes): v6+ needs multi-block
 * ECC interleaving and version-info bits, unimplemented here, so a larger payload
 * throws and qrDraw shows "(QR unavailable)" rather than painting a silently
 * unscannable code. The pairing payload is ~78 bytes max (dotted-quad host + 32-hex
 * token), well within v5. */
"function qrGen(s){var EXP=new Array(512),LOG=new Array(256),x=1,i;"
"for(i=0;i<255;i++){EXP[i]=x;LOG[x]=i;x<<=1;if(x&256)x^=285}"
"for(i=255;i<512;i++)EXP[i]=EXP[i-255];"
"function mul(a,b){return(a===0||b===0)?0:EXP[LOG[a]+LOG[b]]}"
"function rsGen(n){var g=[1],j;for(i=0;i<n;i++){var ng=new Array(g.length+1).fill(0);"
"for(j=0;j<g.length;j++){ng[j]^=mul(g[j],EXP[i]);ng[j+1]^=g[j]}g=ng}return g}"
"var CAP=[[19,7],[34,10],[55,15],[80,20],[108,26]];"
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
"if(f!==0)for(var j=0;j<eccCW;j++)ecc[j]^=mul(gen[eccCW-1-j],f)}"
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
"else if(s=='half'){clearInterval(syncPoll);syncPoll=null;$('hoststatus').textContent='They got your records, but theirs did not come back. Nothing was lost -- sync again to finish.';setView(view)}"
"else if(s=='again'){clearInterval(syncPoll);syncPoll=null;$('hoststatus').textContent='Synced, but one more round is needed to match exactly. Nothing was lost -- sync again.';setView(view)}"
"else if(s=='timeout'){clearInterval(syncPoll);syncPoll=null;$('hoststatus').textContent='No device joined in time. Try again.'}},1500)}"
"function syncJoinPane(){$('syncpick').style.display='none';$('syncfile').style.display='none';$('syncjoin').hidden=false;$('joinstatus').textContent='';$('jaddr').focus()}"
/* file export/import: no network -- carry the whole index as one .aisb file */
"function fileExport(){var a=document.createElement('a');a.href='/api/export-bundle';a.download='ais-export.aisb';document.body.appendChild(a);a.click();a.remove()}"
"async function fileImport(f){var b=await f.arrayBuffer();var r=await fetch('/api/import-bundle',{method:'POST',body:b});"
"if((await r.text()).trim()=='merged'){closeSync();setView(view);alert('Imported. Records merged.')}else{alert('Import failed. Is it an .aisb file from Export?')}}"
/* folder auto-sync: remember the path, run a pass on demand + on load + after a save */
/* The saved folder comes from the SERVER (<index>/syncfolder), not localStorage:
 * clearing browser data must not switch off the user's backup, and the app and the
 * CLI must see the setting. syncFolderSaved() answers from a value fetched at load. */
"var syncFld='';"
"function syncFolderSaved(){return syncFld}"
"async function syncFolderLoad(){try{syncFld=(await(await fetch('/api/sync-folder')).text()).trim()}catch(e){syncFld=''}"
"if(syncFld){$('syncfld').value=syncFld;syncFolderRun(true)}}"
"async function syncFolderRun(silent,force){var p=($('syncfld').value||'').trim();"
"if(!p){if(!silent)$('syncfldmsg').textContent='Enter a folder path.';return}"
"if(!silent)$('syncfldmsg').textContent='Syncing...';"
"$('syncfldany').style.display='none';"
"try{var r=await fetch('/api/sync-folder'+(force?'?force=1':''),{method:'POST',body:p});"
"var s=(await r.text()).trim();"
"if(s=='synced'){syncFldSaid='';syncFld=p;$('syncfldmsg').textContent='Synced.';setView(view);return}"
/* A background pass reports its failure too: a folder sync that quietly stops
 * working is the failure this whole path exists to prevent. */
"var m={'no such folder':'No such folder. Create it first, or check the drive is plugged in.',"
"'not a folder':'That path is a file, not a folder.',"
"'cannot read that folder':'That folder cannot be read. Check the drive and permissions.',"
"'folder empty':'That folder holds no device copies at all, though we have synced with it before. The drive may not be mounted, or it was emptied or replaced.',"
"'cannot write':'Merged what was there, but could not write into that folder: it may be read-only or full.'};"
"var msg=m[s]||'Sync failed.';$('syncfldmsg').textContent=msg;"
"$('syncfldany').style.display=(s=='folder empty')?'':'none';"
/* The Sync sheet is hidden most of the time, so a message written only there is no
 * report at all. Say it out loud. */
"syncFolderAlert(msg)}"
"catch(e){syncFolderAlert('Sync failed.');$('syncfldmsg').textContent='Sync failed.'}}"
/* Once per problem: the pass runs on load and after every save, and repeating the
 * same sentence each time trains the user to ignore it. */
"var syncFldSaid='',syncFldTimer=null;"
/* This toast is shared with delete/undo, so hand it back: clear the timer or a
 * later Deleted toast vanishes early, restore the button or that delete is
 * offered with no Undo at all. */
"function syncFolderAlert(msg){if(msg==syncFldSaid)return;syncFldSaid=msg;"
"var t=$('toast');t.firstChild.textContent=msg;$('toastundo').hidden=true;t.hidden=false;"
"if(syncFldTimer)clearTimeout(syncFldTimer);"
"syncFldTimer=setTimeout(function(){syncFldTimer=null;t.hidden=true;$('toastundo').hidden=false},6000)}"
"async function syncJoinGo(){var url=$('jaddr').value.trim(),tok=$('jtok').value.trim();"
"if(!url||!tok){$('joinstatus').textContent='Enter an address and a token.';return}"
"$('joinstatus').textContent='Syncing...';"
"var r=await fetch('/api/sync/join',{method:'POST',body:url+'\\n'+tok});var s=(await r.text()).trim();"
"if(s=='merged'){$('joinstatus').textContent='Synced. Both devices now have the same records.';setView(view)}"
"else if(s=='half'){$('joinstatus').textContent='Got their records, but yours did not reach them. Nothing was lost -- sync again to finish.';setView(view)}"
"else if(s=='bad url')$('joinstatus').textContent='That address looks wrong. Use http://host:port.';"
"else $('joinstatus').textContent='Could not sync. Same Wi-Fi? Check the host is waiting and the token is right.'}"
"$('syncbtn').onclick=openSync;$('synccancel').onclick=closeSync;"
"$('synchostbtn').onclick=syncHost;$('syncjoinbtn').onclick=syncJoinPane;$('jgo').onclick=syncJoinGo;"
"$('expbtn').onclick=fileExport;$('impbtn').onclick=function(){$('fileimp').click()};"
"$('fileimp').onchange=function(){if(this.files[0])fileImport(this.files[0]);this.value=''};"
"$('syncfldbtn').onclick=function(){syncFolderRun(false,0)};"
"$('syncfldany').onclick=function(){syncFolderRun(false,1)};"
"syncFolderLoad();"
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

/* The shared folder this index syncs with, kept in <index>/syncfolder -- the same
 * file the app reads and writes, so page, app and CLI agree. Per-device by nature (a
 * mount point on THIS machine), so doc/SYNC.md lists it among the files not to sync. */
static void serve_save_syncfolder(const ais *a, const char *path)
{
    char p[AIS_PATH_MAX];
    FILE *f;

    if (snprintf(p, sizeof p, "%s/syncfolder", a->dir) >= (int)sizeof p)
        return;
    f = fopen(p, "w");
    if (f == NULL)
        return;                       /* best-effort: the sync itself has worked */
    fprintf(f, "%s\n", path);
    fclose(f);
}

/* "" when none is set. */
static void serve_load_syncfolder(const ais *a, char *out, size_t osz)
{
    char p[AIS_PATH_MAX];
    FILE *f;
    size_t n;

    out[0] = '\0';
    if (snprintf(p, sizeof p, "%s/syncfolder", a->dir) >= (int)sizeof p)
        return;
    f = fopen(p, "r");
    if (f == NULL)
        return;
    if (fgets(out, (int)osz, f) == NULL)
        out[0] = '\0';
    fclose(f);
    n = strlen(out);
    while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r' ||
                     out[n-1] == ' '  || out[n-1] == '\t'))
        out[--n] = '\0';
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

/* Copy the value of header NAME (case-insensitive) out of the NUL-terminated header
 * block HDRS into OUT (front-trimmed, truncated to fit). Returns 1 if the header is
 * present. Used for the CSRF origin check. */
static int http_header(const char *hdrs, const char *name, char *out, size_t outsz)
{
    size_t nlen = strlen(name);
    const char *p = hdrs;

    if (outsz == 0)
        return 0;
    out[0] = '\0';
    while (*p != '\0') {
        if (strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
            const char *v = p + nlen + 1;
            size_t i = 0;
            while (*v == ' ' || *v == '\t')
                v++;
            while (v[i] != '\0' && v[i] != '\r' && v[i] != '\n' && i + 1 < outsz) {
                out[i] = v[i];
                i++;
            }
            out[i] = '\0';
            return 1;
        }
        p = strchr(p, '\n');            /* next header line */
        if (p == NULL)
            break;
        p++;
    }
    return 0;
}

/* ---- get: stream each matching record's values to the socket ------------ */
struct sink { ais *a; int fd; int meta; };

/* keys-of-id: which visible tags is record ID filed under? Mirrors the embed
 * layer's keysOf -- walk the tags, and for each ask ais_get whether ID is a
 * member. O(tags). Space-separated into BUF ("" if none; a full buffer drops
 * the tail rather than overflowing). */
struct keyhit { long want; int found; };
static int keyhit_cb(long id, void *vp)
{
    struct keyhit *k = vp;
    if (id == k->want) { k->found = 1; return 1; }   /* found: stop the scan */
    return 0;
}
struct keysof { ais *a; long want; char *buf; size_t sz; size_t len; };
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
        int n = snprintf(c->buf + c->len, c->sz - c->len, "%s%s",
                         c->len ? " " : "", key);
        if (n > 0 && (size_t)n < c->sz - c->len)
            c->len += (size_t)n;
        else
            c->buf[c->len] = '\0';             /* would not fit: keep what did */
    }
    return 0;
}
static void keys_of(ais *a, long id, char *buf, size_t sz)
{
    struct keysof c;
    c.a = a; c.want = id; c.buf = buf; c.sz = sz; c.len = 0;
    buf[0] = '\0';
    ais_tags(a, keysof_tag, &c);
}

/* A multi-line value is stored as a plain-text document blob (blobs/<ts>.txt) whose
 * PATH is the record value; the GUI must show the CONTENT. If VALUE is such a blob,
 * read it (capped) and return "aisdoc:<base64>" in OUT: base64 carries the bytes
 * with no newline or '|', so the line-based wire and the client's record split stay
 * intact. Otherwise return VALUE. Single-threaded, so a static read buffer is safe. */
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
    char id_buf[32];
    const char *v = show_value(s->a, value, vbuf, sizeof vbuf);
    int n = snprintf(id_buf, sizeof id_buf, "%ld|", id);
    if (n <= 0)
        return 0;
    write_all(s->fd, id_buf, (size_t)n);
    if (s->meta) {                 /* id|keys|value: the recall view shows tags */
        static char kbuf[AIS_LINE_MAX];
        keys_of(s->a, id, kbuf, sizeof kbuf);
        write_all(s->fd, kbuf, strlen(kbuf));
        write_all(s->fd, "|", 1);
    }
    write_all(s->fd, v, strlen(v));
    write_all(s->fd, "\n", 1);
    return 0;
}

static int on_id(long id, void *vp)
{
    struct sink *s = vp;
    ais_record(s->a, id, on_value, s);
    return 0;
}

/* Get records under the keys: AND (intersection) by default, OR (union) when
 * want_or is set (the "Match any key" box). No automatic relaxation. */
static void do_get(ais *a, char *keys, int want_or, long after, int count,
                   int meta, int fd)
{
    char *kv[AIS_KEYS_MAX];
    int nkeys = 0;
    char *tok, *save;
    struct sink s;

    for (tok = strtok_r(keys, " ", &save); tok != NULL && nkeys < AIS_KEYS_MAX;
         tok = strtok_r(NULL, " ", &save))
        kv[nkeys++] = tok;

    s.a = a; s.fd = fd; s.meta = meta;
    /* Keyset page: emit COUNT matches with id > AFTER (0/0 = the whole set), so
     * the page infinite-scrolls a large result instead of loading it whole. */
    if (nkeys > 0)
        ais_get_page(a, kv, nkeys, want_or ? AIS_OR : AIS_AND, after, count, on_id, &s);
}

/* ---- put: the WHOLE body is one record ----------------------------------
 * A pasted block is one entry, not one record per line: ais_put_value() keeps a
 * single line as a plain record and routes a multi-line value to a blob. 1 if
 * stored, 0 if empty/failed. */
static long do_put(ais *a, const char *keys, char *body)
{
    return ais_put_value(a, keys, body) >= 0 ? 1 : 0;
}

/* Encrypt save (?enc=1): BODY is "passphrase\nvalue..." -- the passphrase rides the
 * POST body, never the URL, so it stays out of the browser history. Encrypts VALUE
 * under it and puts the "aisc:" marker. 1 if stored, 0 on malformed/failure/no-crypto. */
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
    if (n < 0)
        return 0;
    if (n >= (int)sizeof(line)) {        /* a blob-expanded value + big keys can exceed the
                                          * buffer; snprintf returns the untruncated length,
                                          * so writing n bytes would over-read past `line`.
                                          * Clamp, keeping the row newline-framed so the
                                          * client's line parser stays in sync. */
        n = (int)sizeof(line) - 1;
        line[n - 1] = '\n';
    }
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

/* Serve an external asset <webdir>/<name> if present, so the look can be edited as
 * plain files instead of the embedded PAGE. webdir = $AIS_WEB, else "gui/web". NAME
 * must be one safe filename (letters/digits/._-, no '/' or ".."), so the browser
 * cannot escape the dir. Returns 1 if served. */
static int serve_asset(int fd, const char *name)
{
    /* $AIS_WEB is the documented way to serve the app/ page instead of the embedded
     * one (app/README.md, doc/android-install.md). Traversal is impossible: NAME is
     * one safe filename, checked below. */
    const char *webdir = getenv("AIS_WEB");
    char path[AIS_PATH_MAX], buf[8192];
    FILE *fp;
    size_t n;
    const char *p;

    if (webdir == NULL || webdir[0] == '\0')
        webdir = "gui/web";               /* the dev default */
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
 * Host: fork an ephemeral child running sync_serve() single-shot, so the
 * single-threaded HTTP loop is not blocked while it waits for a peer; the parent
 * returns the pairing info (URL + token) at once and the page renders a QR of it.
 * The child exits after one peer or the timeout; the parent reaps it. Join:
 * synchronous sync_pull() -- a LAN merge is quick. Both use bidir=1, the symmetric
 * exchange the mobile app uses (both devices converge in one round).
 */
#define SERVE_SYNC_BIDIR    1     /* symmetric exchange (matches the mobile Sync) */
#define SERVE_SYNC_TIMEOUT 120    /* seconds the host child waits for one peer    */

static volatile sig_atomic_t sync_child = -1;   /* live Host child pid, or -1 (one at a time) */
static volatile sig_atomic_t sync_last  = -2;   /* last Host outcome: 0 served, 1 half, else not */

/* Reap the Host child if it has finished so it leaves no zombie, remembering its
 * outcome (the child exits 0 when a peer synced, non-zero on timeout/error). Called
 * from a SIGCHLD handler and from the routes; WNOHANG and the pid guard make a
 * double call harmless. Only waitpid and plain assignment here, so it is
 * async-signal-safe. */
static void sync_reap(void)
{
    pid_t pid = (pid_t)sync_child;
    int st;
    if (pid <= 0)
        return;
    if (waitpid(pid, &st, WNOHANG) == pid) {
        sync_last = !WIFEXITED(st) ? -2
                  : (WEXITSTATUS(st) == 0) ? 0
                  : (WEXITSTATUS(st) == 2) ? 1        /* half: see sync_status */
                  : (WEXITSTATUS(st) == 3) ? 2        /* run it again */
                  : -2;
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
        ais_on_discard(&fresh, ais_doc_discard_cb, fresh.dir);
        rc = sync_serve(&fresh, port, token, SERVE_SYNC_TIMEOUT, SERVE_SYNC_BIDIR);
        ais_close(&fresh);
        /* The parent has only an exit status to read: 0 = converged, 2 = half (the
         * peer got ours, we did not get theirs), 3 = both merged but one more round
         * is needed, 1 = timeout/error. */
        _exit(rc == 0 ? 0
              : rc == AIS_SYNC_PARTIAL ? 2
              : rc == AIS_SYNC_AGAIN   ? 3 : 1);
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
    else if (sync_last == 1) s = "half\n";    /* they got ours, we did not get theirs */
    else if (sync_last == 2) s = "again\n";   /* both merged; one more round needed */
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
    /* "half" is a SUCCESS with an unfinished second leg: their records are here. */
    if (rc == 0)                        write_all(fd, "merged\n", 7);
    else if (rc == AIS_SYNC_PARTIAL)    write_all(fd, "half\n", 5);
    else                                write_all(fd, "could not connect or wrong token\n", 33);
}
#endif /* SERVE_HAVE_SYNC */

/* ---- one request -------------------------------------------------------- */
/* Discard a payload this index made (encrypted or document blob) before its
 * record is tombstoned, so the content does not outlive it. VP is the index
 * dir; a value pointing at one of the user's own files is untouched. */
static int serve_shred_value(long id, const char *value, void *vp)
{
    (void)id;
    ais_doc_discard((const char *)vp, value);
    return 0;
}

/* Same, per MATCHED RECORD: the bulk-delete pre-pass. VP is the ais handle. */
static int serve_shred_id(long id, void *vp)
{
    ais *a = vp;
    ais_record(a, id, serve_shred_value, (void *)a->dir);
    return 0;
}

static void handle(ais *a, int fd)
{
    char buf[AIS_LINE_MAX];
    char nokeys[1] = "";
    ssize_t n;
    char *method, *path, *query, *body, *keys = nokeys, *sp;
    int want_or = 0;                      /* "Match any key" -> AIS_OR; default AND+relax */
    int enc = 0;                          /* ?enc=1 -> encrypt the value before storing */
#ifdef SERVE_HAVE_SYNC
    int force = 0;                        /* ?force=1 -> accept a remembered folder gone empty */
#endif
    int forget = 0;                       /* ?forget=1 -> compaction also drops the
                                           * tombstone hashes (see /api/compact)   */
    long reqid = 0;                       /* ?id= for /api/del and /api/update      */
    long before = 0;                      /* ?before= cursor for /api/timeline paging */
    long after = 0;                       /* ?after= id cursor for /api/get paging    */
    int meta = 0;                         /* ?meta=1: /api/get lines carry keys too   */
    long afterc = 0;                      /* ?afterc= count cursor for /api/tags paging */
    char *afterk = nokeys;                /* ?afterk= key cursor for /api/tags paging */
    long body_len = 0;                    /* Content-Length, for a big POST body      */
    int  count = 0;                       /* ?count= page size (0 => engine default)  */
    char *tlfrom = nokeys, *tlto = nokeys; /* ?from= ?to= date range (YYYY-MM-DD)      */
    int  cross_site = 0;                  /* CSRF: request from another web origin    */

    n = SOCK_READ(fd, buf, sizeof(buf) - 1);   /* first read: usually the whole request */
    if (n <= 0)
        return;
    buf[n] = '\0';

    /* The header block itself can arrive split across TCP segments. Read until the
     * blank line that ends the headers is in hand, so Content-Length is parsed from
     * the COMPLETE header block and a header-fragmented request is not misparsed as
     * an empty POST. Bounded by buf; the per-socket recv timeout (ais_serve) caps
     * the wait. */
    while (strstr(buf, "\r\n\r\n") == NULL && (size_t)n < sizeof(buf) - 1) {
        ssize_t k = SOCK_READ(fd, buf + n, sizeof(buf) - 1 - (size_t)n);
        if (k <= 0)
            break;
        n += k;
        buf[n] = '\0';
    }

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

        /* CSRF guard: a browser tags every request with Sec-Fetch-Site. The GUI's
         * own fetches are "same-origin"; a direct address-bar hit is "none". Any
         * other value ("cross-site"/"same-site") is a DIFFERENT web page driving our
         * localhost API with the user's ambient authority, which could push the whole
         * index to an attacker (via /api/sync/join) or inject/delete records. Refuse
         * those on the API. Older clients without the header fall back to an Origin
         * check; non-browser callers (curl, the CLI, the tests) send neither. */
        {
            char hv[64];
            if (http_header(buf, "Sec-Fetch-Site", hv, sizeof hv)) {
                if (strcmp(hv, "same-origin") != 0 && strcmp(hv, "none") != 0)
                    cross_site = 1;
            } else if (http_header(buf, "Origin", hv, sizeof hv)) {
                /* Match the WHOLE host, not a 16-char prefix: a bare strncmp would
                 * accept the attacker-controlled http://localhost.evil.example as
                 * same-origin. The literal must be followed by end-of-string or a
                 * ':' port, nothing else. */
                int local = (strncmp(hv, "http://127.0.0.1", 16) == 0 ||
                             strncmp(hv, "http://localhost", 16) == 0) &&
                            (hv[16] == '\0' || hv[16] == ':');
                if (!local)
                    cross_site = 1;
            }
        }
        if (he != NULL) *he = keep;
    }

    body = strstr(buf, "\r\n\r\n");       /* split headers from body first... */
    if (body != NULL) { *body = '\0'; body += 4; }
    else              { body = buf + n; }

    /* The reads above can hold only the headers: a browser's fetch() routinely sends
     * the POST body in a later packet. Read until the whole body is in, or we parse
     * an empty value and close the socket with the body still unread, which resets
     * the connection ("failed to fetch") with nothing saved. Bounded by buf. */
    if (body_len > 0) {
        ssize_t have = n - (body - buf);
        while (have < body_len && (size_t)n < sizeof(buf) - 1) {
            ssize_t k = SOCK_READ(fd, buf + n, sizeof(buf) - 1 - (size_t)n);
            if (k <= 0) break;
            n += k;
            have += k;
        }
        buf[n] = '\0';

        /* The body did not fit the request buffer. Reading only what fits TRUNCATES
         * the value, and leaving the tail unread RSTs the peer on close. Drain the
         * remainder off the socket and refuse with 413. The one large-body route,
         * /api/import-bundle, streams its own body (reads Content-Length directly),
         * so exempt it; its path is still intact in the header block. */
        if (have < body_len && strstr(buf, "/api/import-bundle") == NULL) {
            char sink[4096];
            while (have < body_len) {
                ssize_t k = SOCK_READ(fd, sink, sizeof(sink));
                if (k <= 0) break;
                have += k;
            }
            {
                static const char e[] = "HTTP/1.0 413 Payload Too Large\r\n"
                    "Connection: close\r\n\r\nvalue too large for one request "
                    "(~64 KB max) -- split it, or use a document/import\n";
                write_all(fd, e, sizeof(e) - 1);
            }
            return;
        }
    }

    method = buf;                         /* ...then parse the request line   */
    path = strchr(buf, ' ');
    if (path == NULL)
        return;
    *path++ = '\0';
    sp = strchr(path, ' ');
    if (sp != NULL) *sp = '\0';
    query = strchr(path, '?');
    if (query != NULL) *query++ = '\0';

    /* CSRF: refuse cross-origin browser calls to the API surface (see the header
     * parse above). The page + assets stay reachable (direct nav is "none"). */
    if (cross_site && strncmp(path, "/api/", 5) == 0) {
        static const char e[] = "HTTP/1.0 403 Forbidden\r\n"
            "Connection: close\r\n\r\ncross-origin request refused\n";
        write_all(fd, e, sizeof(e) - 1);
        return;
    }

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
        } else if (strncmp(query, "forget=", 7) == 0) {
            forget = (query[7] == '1');
#ifdef SERVE_HAVE_SYNC
        } else if (strncmp(query, "force=", 6) == 0) {
            force = (query[6] == '1');       /* sync a remembered folder that is now empty */
#endif
        } else if (strncmp(query, "id=", 3) == 0) {
            reqid = atol(query + 3);
        } else if (strncmp(query, "before=", 7) == 0) {
            before = atol(query + 7);
        } else if (strncmp(query, "after=", 6) == 0) {
            after = atol(query + 6);
        } else if (strncmp(query, "meta=", 5) == 0) {
            meta = (query[5] == '1');
        } else if (strncmp(query, "afterc=", 7) == 0) {
            afterc = atol(query + 7);
        } else if (strncmp(query, "afterk=", 7) == 0) {
            afterk = query + 7;
            url_decode(afterk);
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
        do_get(a, keys, want_or, after, count, meta, fd);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/put") == 0) {
        char msg[64];
        long c = enc ? do_put_enc(a, keys, body) : do_put(a, keys, body);
        if (c <= 0) {
            /* Saving nothing is a FAILURE, not a 200 with a count of zero: on a
             * 200 both pages close the sheet as if the value had been stored, and
             * for an encrypted save on a build with no crypto module that loses
             * the thing the user was trying to protect. */
            static const char e[] = "HTTP/1.0 500 Internal Server Error\r\n"
                "Connection: close\r\n\r\nnothing was saved\n";
            write_all(fd, e, sizeof(e) - 1);
        } else {
            int m = snprintf(msg, sizeof(msg), "saved %ld record(s)\n", c);
            send_head(fd, "text/plain");
            if (m > 0)
                write_all(fd, msg, (size_t)m);
        }
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
        /* delete record ?id=N (the handle from the id|value lines). Shred any
         * encrypted blob FIRST, as the CLI does: deleting a secret must not leave
         * its ciphertext on disk, and exportable, afterwards. */
        if (reqid > 0)
            ais_record(a, reqid, serve_shred_value, (void *)a->dir);
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
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/untag") == 0) {
        /* Remove the tag ?keys=KEY from every record, destroying nothing: the
         * non-destructive half of the pair below, and the page must offer BOTH or a
         * user who only wants the tag gone reaches for the delete. ONE tag, not the
         * whitespace-separated list every other endpoint takes: key_encode would fold
         * "a b" to "a_b" and report a cheerful 200 for a no-op on a tag that cannot
         * exist. */
        int nu = (keys[0] != '\0' && strcspn(keys, " \t") == strlen(keys))
                 ? ais_untag_key(a, keys) : -1;
        if (nu >= 0) {
            char msg[64];
            int len = snprintf(msg, sizeof msg, "untagged %d\n", nu);
            send_head(fd, "text/plain");
            write_all(fd, msg, (size_t)len);
        } else {
            static const char e[] = "HTTP/1.0 400 Bad Request\r\n"
                "Connection: close\r\n\r\ncannot untag\n";
            write_all(fd, e, sizeof(e) - 1);
        }
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/del-under") == 0) {
        /* DELETE every record filed under ?keys=KEY. Shred encrypted blobs first,
         * exactly as /api/del and the CLI do. */
        if (keys[0] != '\0' && strcspn(keys, " \t") == strlen(keys)) {
            char *k1[1];
            int nd;
            k1[0] = keys;
            ais_get(a, k1, 1, AIS_AND, serve_shred_id, (void *)a);
            nd = ais_del_key(a, keys);
            if (nd >= 0) {
                char msg[64];
                int len = snprintf(msg, sizeof msg, "deleted %d\n", nd);
                send_head(fd, "text/plain");
                write_all(fd, msg, (size_t)len);
            } else {
                static const char e[] = "HTTP/1.0 400 Bad Request\r\n"
                    "Connection: close\r\n\r\ncannot delete\n";
                write_all(fd, e, sizeof(e) - 1);
            }
        } else {
            static const char e[] = "HTTP/1.0 400 Bad Request\r\n"
                "Connection: close\r\n\r\ncannot delete\n";
            write_all(fd, e, sizeof(e) - 1);
        }
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/version") == 0) {
        /* A browser cannot run `ais --version`, and which number moved is the first
         * thing a bug report needs. Engine and on-disk format; the page adds its own. */
        char vbuf[128];
        int vn = snprintf(vbuf, sizeof vbuf, "engine: %s\nformat: v%d\n",
                          ais_version(), AIS_FORMAT_VERSION);
        send_head(fd, "text/plain");
        if (vn > 0)
            write_all(fd, vbuf, (size_t)vn);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/stats") == 0) {
        /* The same three lines as `ais --stats`. The GUI needs the deleted count to
         * say what "clean up" would reclaim. */
        FILE *sp = tmpfile();
        send_head(fd, "text/plain");
        if (sp != NULL) {
            char sbuf[512];
            size_t n;
            if (ais_stats(a, sp) == 0 && fflush(sp) == 0) {
                rewind(sp);
                while ((n = fread(sbuf, 1, sizeof sbuf, sp)) > 0)
                    write_all(fd, sbuf, n);
            }
            fclose(sp);
        }
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/compact") == 0) {
        /* Reclaim deleted records. ?forget=1 also strips the tombstone hashes, so the
         * deletions stay in force here but stop travelling and stop being testable.
         * Reachable from the GUI because a phone has no CLI. */
        if ((forget ? ais_compact_purge(a) : ais_compact(a)) == 0) {
            send_head(fd, "text/plain");
            write_all(fd, forget ? "cleaned and forgotten\n" : "cleaned\n",
                      forget ? 22 : 8);
        } else {
            static const char e[] = "HTTP/1.0 500 Internal Server Error\r\n"
                "Connection: close\r\n\r\ncould not clean up\n";
            write_all(fd, e, sizeof(e) - 1);
        }
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/keys") == 0) {
        /* the visible tags of record ?id=N (for the edit dialog's chips) */
        static char kb[AIS_LINE_MAX];
        send_head(fd, "text/plain");
        if (reqid > 0) {
            keys_of(a, reqid, kb, sizeof kb);
            write_all(fd, kb, strlen(kb));
        }
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
        /* keyset page over the busiest-first cloud: afterk "" = the first page */
        ais_tags_page(a, afterc, afterk[0] ? afterk : NULL, count, tag_sink, &s);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/where") == 0) {
        send_head(fd, "text/plain");          /* the current store (index dir) */
        write_all(fd, a->dir, strlen(a->dir));
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/sync-folder") == 0) {
        char sf[AIS_PATH_MAX];                /* "" when none is set */
        serve_load_syncfolder(a, sf, sizeof sf);
        send_head(fd, "text/plain");
        write_all(fd, sf, strlen(sf));
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
                ais_on_discard(a, ais_doc_discard_cb, a->dir);   /* a reopen clears it */
                write_all(fd, e, sizeof(e) - 1);
                return;                        /* accept loop closes fd */
            }
            ais_on_discard(a, ais_doc_discard_cb, a->dir);
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
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/sync-folder") == 0) {
        /* One folder-sync pass: body = the shared folder path (a Syncthing / cloud
         * folder). Import every peer bundle, (re)write our own. Local single-user. */
        char *nd = body;
        size_t bl;
        while (*nd == ' ' || *nd == '\t') nd++;
        bl = strlen(nd);
        while (bl > 0 && (nd[bl-1] == '\r' || nd[bl-1] == '\n' ||
                          nd[bl-1] == ' '  || nd[bl-1] == '\t'))
            nd[--bl] = '\0';
        /* NB: `force` is parsed in the query loop above -- by the time an endpoint
         * runs, the loop has walked `query` to NULL. */
        int frc = (nd[0] == '\0') ? -1
                : sync_folder_once_force(a, nd, force);
        if (frc == 0) {
            /* Remember it HERE, in <index>/syncfolder: where the app keeps it and
             * what doc/SYNC.md documents, and not in localStorage, which the CLI and
             * the app cannot see and clearing browser data wipes. Written only after
             * a pass that worked, so a bad path is never persisted. */
            serve_save_syncfolder(a, nd);
            send_head(fd, "text/plain");
            write_all(fd, "synced\n", 7);
        } else {
            /* Name the reason: the user cannot tell a typo from an unmounted drive
             * from a read-only share, and each has a different remedy. */
            const char *why;
            char head[256];
            switch (frc) {
            case AIS_FOLDER_MISSING:   why = "no such folder"; break;
            case AIS_FOLDER_NOTDIR:    why = "not a folder";   break;
            case AIS_FOLDER_STAT:      why = "cannot read that folder"; break;
            case AIS_FOLDER_STRANGER:  why = "folder empty";  break;
            case AIS_FOLDER_NOWRITE:   why = "cannot write";   break;
            default:                   why = "folder sync failed"; break;
            }
            snprintf(head, sizeof head, "HTTP/1.0 400 Bad Request\r\n"
                     "Connection: close\r\n\r\n%s\n", why);
            write_all(fd, head, strlen(head));
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

    /* best-effort: open the page in the user's browser (GUI-wrapper behaviour).
     * macOS `open`, Linux `xdg-open`; ignored if neither exists. AIS_NO_OPEN
     * suppresses it, for agents, screenshots, scripts and CI that drive the server
     * headlessly and must not spawn a browser window. */
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
        /* Bound every recv on this connection. The loop is single-threaded, so one
         * accepted client that sends nothing -- a browser's preconnect socket, or a
         * peer that announces a Content-Length then withholds the body -- would park
         * the sole thread in recv() forever and wedge the whole GUI. A timed-out read
         * returns <=0, which handle() treats as a dropped client, so the loop
         * recovers. This bounds the stall; it does not make handling concurrent (sync
         * Host state lives in this process's globals, so per-connection forking is
         * out). Localhost requests arrive in milliseconds, so 5s is far above any
         * real client. */
#ifdef _WIN32
        {
            DWORD tv = 5000;   /* ms */
            setsockopt((SOCKET)cfd, SOL_SOCKET, SO_RCVTIMEO,
                       (const char *)&tv, sizeof(tv));
        }
#else
        {
            struct timeval tv;
            tv.tv_sec = 5;
            tv.tv_usec = 0;
            setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }
#endif
        handle(a, cfd);
        SOCK_CLOSE(cfd);
    }
    /* not reached */
}
