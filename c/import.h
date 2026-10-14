/* import.h -- importers for files other programs wrote: links (the Netscape
 * bookmarks HTML that browsers and most bookmark services export, or a CSV
 * with a url column: Pocket, Instapaper, Raindrop, or any spreadsheet) and
 * Google Keep notes (a Takeout directory, one .json per note).
 *
 * ais never guesses tags. The keys carried over are names the user made --
 * bookmark folder names, a TAGS attribute, a CSV's tags and folder columns,
 * Keep label names -- word-tokenized and normalized the way the engine files
 * keys, plus ONE constant marker key per source, so a batch is findable and
 * reversible with --del-under: "bookmark" for HTML, "keep" for Keep, and for a
 * CSV the exporter its header names ("pocket", "instapaper", "raindrop") or
 * "link" when it names none. An app's own buckets (Unread, Archive, Starred,
 * Unsorted) and a Pocket status are not names the user made: dropped. */
#ifndef AIS_IMPORT_H
#define AIS_IMPORT_H

#include <stddef.h>
#include <stdio.h>

#include "ais.h"

/* What import_links_fp returns besides a count: the file was not a links file. */
enum {
    AIS_IMPORT_NOURL = -2,   /* a CSV whose header names no url column       */
    AIS_IMPORT_TMP   = -3,   /* no temp file for the spool                    */
    AIS_IMPORT_UTF16 = -4,   /* UTF-16 text (a spreadsheet's "Unicode" save)  */
    AIS_IMPORT_ZIP   = -5    /* a .zip (Pocket's download): extract it first  */
};

/* Import the links in IN, open for reading at its start: sniffs HTML from CSV.
 * Each link becomes one record: value "URL title" (the URL alone when the title
 * is empty or repeats it), keys = the user's names (above) plus the marker, the
 * save time = the export's own time column (time_added, Timestamp, created,
 * ADD_DATE) when it carries one. A row without a URL, or one too long for a
 * store line, counts in *SKIPPED. A control character in any field becomes a
 * space, so a field cannot forge a line of the import stream. Prints nothing.
 * Returns the records put (>= 0), or one of the codes above. */
long import_links_fp(ais *a, FILE *in, long *skipped);

/* The CLI's --import-bookmarks: import_links_fp over FILE, then the one-line
 * report; dies with the reason when FILE cannot be opened or is not a links
 * file. Returns the records put. */
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
