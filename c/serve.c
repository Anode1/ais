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
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "ais.h"
#include "common.h"
#include "serve.h"

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
"<meta name=viewport content='width=device-width,initial-scale=1'><title>AIS</title>"
"<style>body{font:15px system-ui;margin:2rem auto;max-width:640px;padding:0 1rem}"
"h1{font-size:1.3rem}.row{display:flex;gap:.5rem}#q{flex:1;font:inherit;padding:.6rem}"
"button{font:inherit;padding:.55rem 1rem;cursor:pointer}#out{margin-top:1rem}"
".hit{padding:.7rem .2rem;border-bottom:1px solid #ddd}.hit:last-child{border-bottom:0}"
".hit:hover{background:#fafafa}.hit a{color:#1a0dab;text-decoration:none}"
".muted{color:#777}#addbtn{margin-top:2rem;background:none;border:0;color:#1a0dab;cursor:pointer;font:inherit;padding:0}"
"#add{display:none;margin-top:.5rem;border-top:1px solid #eee;padding-top:1rem}"
"#add textarea{width:100%;box-sizing:border-box;font:inherit;padding:.5rem;height:5rem}</style>"
"<h1>AIS</h1>"
"<div class=row><input id=q placeholder='type keys to recall...' autofocus>"
"<button onclick=get()>Get</button></div><div id=out></div>"
"<button id=addbtn onclick=toggleAdd()>+ add</button>"
"<div id=add><textarea id=v placeholder='values, one per line'></textarea>"
"<button onclick=put()>Put values</button> <span id=m class=muted></span></div>"
"<script>"
"var $=function(i){return document.getElementById(i)};"
"function render(t,q,ms){var b=$('out');b.innerHTML='';"
"var L=t.split('\\n').filter(function(s){return s.length});"
"var h=document.createElement('div');h.className='muted';"
"h.textContent=L.length+' result'+(L.length==1?'':'s')+(q?' for '+q:'')+' - '+ms.toFixed(1)+' ms';"
"b.appendChild(h);"
"L.forEach(function(ln){var p=ln.indexOf('|'),v=p>=0?ln.slice(p+1):ln,"
"d=document.createElement('div');d.className='hit';"
"if(v.slice(0,4)=='http'){var a=document.createElement('a');a.href=v;a.target='_blank';a.textContent=v;d.appendChild(a);}"
"else{d.textContent=v;}b.appendChild(d);});}"
"var ek=function(){return encodeURIComponent($('q').value)};"
"async function get(){var q=$('q').value,t0=performance.now();"
"var t=await(await fetch('/api/get?keys='+encodeURIComponent(q))).text();"
"render(t,q,performance.now()-t0);}"
"async function put(){$('m').textContent=await(await fetch('/api/put?keys='+ek(),{method:'POST',body:$('v').value})).text();$('v').value='';}"
"function toggleAdd(){var a=$('add');a.style.display=a.style.display=='block'?'none':'block';}"
"$('q').addEventListener('keydown',function(e){if(e.key=='Enter')get();});"
"</script>";

static void write_all(int fd, const char *p, size_t n)
{
    while (n > 0) {
        ssize_t w = write(fd, p, n);
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

static void do_get(ais *a, char *keys, int fd)
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
        ais_get(a, kv, nkeys, AIS_AND, on_id, &s);
}

/* ---- put: each body line is a value under the keys ---------------------- */
static long do_put(ais *a, const char *keys, char *body)
{
    long count = 0;
    char *line, *save;

    for (line = strtok_r(body, "\n", &save); line != NULL;
         line = strtok_r(NULL, "\n", &save)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' '))
            line[--len] = '\0';                 /* browsers send CRLF */
        if (len == 0)
            continue;
        if (ais_put(a, keys, line) >= 0)
            count++;
    }
    return count;
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
    }
    return "text/plain";
}

/* Serve an external asset <webdir>/<name> if present, so the look can be edited
 * as plain files (kul-style) instead of the embedded PAGE. webdir = $AIS_WEB,
 * else "gui/web". NAME must be one safe filename (letters/digits/._-), with no
 * '/' or "..", so the browser cannot escape the dir. Returns 1 if served. */
static int serve_asset(int fd, const char *name)
{
    const char *webdir = getenv("AIS_WEB");
    char path[AIS_PATH_MAX], buf[8192];
    FILE *fp;
    size_t n;
    const char *p;

    if (name[0] == '\0')
        return 0;
    for (p = name; *p != '\0'; p++)
        if (!isalnum((unsigned char)*p) && *p != '.' && *p != '_' && *p != '-')
            return 0;                     /* reject '/', '..', anything unsafe */
    if (webdir == NULL || webdir[0] == '\0')
        webdir = "gui/web";
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

    n = read(fd, buf, sizeof(buf) - 1);   /* assume the request fits (a sketch) */
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

    if (query != NULL) {                  /* keys=... (the only param we read) */
        char *amp = strchr(query, '&');
        if (amp != NULL) *amp = '\0';
        if (strncmp(query, "keys=", 5) == 0) {
            keys = query + 5;
            url_decode(keys);
        }
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/get") == 0) {
        send_head(fd, "text/plain");
        do_get(a, keys, fd);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/put") == 0) {
        char msg[64];
        long c = do_put(a, keys, body);
        int m = snprintf(msg, sizeof(msg), "stored %ld value(s)\n", c);
        send_head(fd, "text/plain");
        if (m > 0)
            write_all(fd, msg, (size_t)m);
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

    signal(SIGPIPE, SIG_IGN);   /* a client hangup must not kill the server */

    sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0)
        return -1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   /* 127.0.0.1 only */
    addr.sin_port = htons((unsigned short)port);
    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(sfd);
        return -1;
    }
    if (listen(sfd, 16) != 0) {
        close(sfd);
        return -1;
    }
    fprintf(stderr, "ais serve: http://127.0.0.1:%d/  (Ctrl-C to stop)\n", port);

    /* best-effort: open the page in the user's browser (this is GUI-wrapper
     * behaviour). macOS `open`, Linux `xdg-open`; ignored if neither exists. */
    {
        char cmd[224];
        int rc;
        snprintf(cmd, sizeof(cmd),
                 "{ xdg-open 'http://127.0.0.1:%d/' || open 'http://127.0.0.1:%d/'"
                 " || cygstart 'http://127.0.0.1:%d/'; } >/dev/null 2>&1 &",
                 port, port, port);     /* xdg-open: Linux, open: macOS, cygstart: Cygwin */
        rc = system(cmd);
        (void)rc;
    }

    for (;;) {
        cfd = accept(sfd, NULL, NULL);
        if (cfd < 0)
            continue;
        handle(a, cfd);
        close(cfd);
    }
    /* not reached */
}
