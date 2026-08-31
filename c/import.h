/* import.h -- importers for files other programs wrote: browser bookmarks
 * (the Netscape HTML that Chrome, Firefox and Safari export) and Google Keep
 * notes (a Takeout directory, one .json per note).
 *
 * ais never guesses tags. The keys carried over are names the user made --
 * bookmark folder names, Keep label names -- word-tokenized and normalized the
 * way the engine files keys, plus ONE constant marker key per importer
 * ("bookmark", "keep"), so the whole batch is findable and reversible:
 * ais --del-under bookmark. */
#ifndef AIS_IMPORT_H
#define AIS_IMPORT_H

#include <stddef.h>

#include "ais.h"

/* Import FILE, a browser's exported bookmarks HTML. Each <A HREF> entry becomes
 * one record: value "URL title" (or just the URL), keys = the enclosing folder
 * names' word tokens plus "bookmark". Prints "imported N[, skipped M]" (a
 * malformed <A> line counts as skipped) and returns N; dies if FILE cannot be
 * opened. */
long import_bookmarks(ais *a, const char *path);

/* Import DIR, an extracted Takeout Keep folder: every *.json note that is not
 * trashed. Value = title and text (or "- item" list lines) joined; a multi-line
 * value goes out of line as a document blob. Keys = the note's label names'
 * word tokens plus "keep". Same report and return as import_bookmarks; dies on
 * anything that is not a directory (a .zip is told to extract first). */
long import_keep(ais *a, const char *dir);

/* Append NAME's whitespace-separated word tokens to KEYS (a space-separated
 * list), each normalized as the engine files it (key_encode: ASCII lowercase,
 * unsafe characters fold to '_'); a token already present, or one that encodes
 * empty, is dropped. 0, or -1 when a token would not fit (KEYS unchanged). */
int import_keys_add(char *keys, size_t ksz, const char *name);

/* Decode the five entities a browser export writes in titles and HREFs --
 * &amp; &lt; &gt; &quot; &#39; -- into OUT; anything else passes through.
 * IN may be OUT (decoding never grows). */
void import_html_entities(const char *in, char *out, size_t osz);

/* Decode one JSON string starting at the opening quote P into OUT (the common
 * escapes and \uXXXX, surrogate pairs included; truncates to fit). Returns the
 * position just past the closing quote, or NULL if the string never closes. */
const char *import_json_string(const char *p, char *out, size_t osz);

#endif /* AIS_IMPORT_H */
