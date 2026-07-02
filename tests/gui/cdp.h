/* cdp.h -- a minimal Chrome DevTools Protocol client, in C99, no dependencies.
 *
 * It speaks CDP straight to a headless Chrome over a WebSocket (the same wire
 * protocol Puppeteer/Playwright use), with no chromedriver middleman and no
 * library beyond the C standard library and POSIX sockets. It knows only the
 * handful of commands a click-and-assert test needs: navigate, evaluate JS,
 * type text, press a key. Not a general framework -- if you need cross-browser
 * or a rich API, use Playwright; this exists to drive one local Chrome from C.
 *
 * Lifecycle: start Chrome yourself with --remote-debugging-port=PORT, then
 *   cdp *c = cdp_open("127.0.0.1", PORT);   // attaches to the first page target
 *   cdp_navigate(c, "http://127.0.0.1:9099/");
 *   cdp_wait_bool(c, "!!document.getElementById('q')", 5000);
 *   ... cdp_insert_text / cdp_key / cdp_eval_bool ...
 *   cdp_close(c);
 *
 * Every call returns 0 on success and -1 on failure (cdp_open returns NULL);
 * a diagnostic is printed to stderr. The handle is not thread-safe: one
 * request/response is in flight at a time, matched by a monotonic id. */
#ifndef CDP_H
#define CDP_H

typedef struct cdp cdp;

/* Attach to the first "page" target of a Chrome already listening on
 * host:port for remote debugging. NULL on failure. */
cdp *cdp_open(const char *host, int port);
void cdp_close(cdp *c);

/* Page.navigate to url. Does not wait for load -- follow with cdp_wait_bool on
 * a page-specific predicate (e.g. an element that only your page defines). */
int cdp_navigate(cdp *c, const char *url);

/* Runtime.evaluate a JS expression that must yield a boolean; *out gets 0/1.
 * Returns -1 if the expression throws or does not evaluate to a boolean. */
int cdp_eval_bool(cdp *c, const char *js, int *out);

/* Poll cdp_eval_bool(js) until it is true or timeout_ms elapses. A transient
 * evaluation error (e.g. during navigation) counts as "not yet", not a failure.
 * Returns 0 once true, -1 on timeout. */
int cdp_wait_bool(cdp *c, const char *js, int timeout_ms);

/* Input.insertText: type text into the currently focused element, as an IME
 * commit would (fires input events; does not synthesize per-key events). */
int cdp_insert_text(cdp *c, const char *text);

/* Input.dispatchKeyEvent: a keyDown+keyUp for a named key. "Enter" is mapped to
 * its virtual key so a keydown listener sees e.key === "Enter". */
int cdp_key(cdp *c, const char *key);

#endif /* CDP_H */
