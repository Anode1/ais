/* help.c -- AIS usage text.
 * usage_short() for a bare invocation and -h; usage_long() for --help.
 * Compiled into the binary (no runtime file dependency). */
#include "help.h"

void usage_short(FILE *out)
{
    fputs(
"usage: ais [-f DIR] [-o] KEY...        get records under ALL keys (default)\n"
"       ais [-f DIR] put VALUE KEY...   file a value under keys  (-R DIR: a folder; - : stdin)\n"
"       ais [-f DIR] find TEXT          search: records whose value contains TEXT\n"
"       ais [-f DIR] add ID VALUE | del ID | del-key KEY | dump | keys | stats | compact\n"
"       ais init                       create a local .ais index here (git-style)\n"
"       ais import < FILE              add 'keys|value' lines (inverse of dump)\n"
"       ais doc KEY... < FILE          save stdin as a document (a blob file)\n"
"       ais project [KEY]              show / set / clear ('') the default project\n"
"  -f DIR  index dir; else $AIS_INDEX, else nearest .ais/, else ~/.local/share/ais\n"
"  -o      match ANY key (OR) instead of ALL (AND)\n"
"  -p KEY  default project key prepended to every put ('' resets; see 'project')\n"
"  -i  interactive add    -y  yes to del/compact    -V  version\n"
"  run 'ais --help' for examples\n",
        out);
}

void usage_long(FILE *out)
{
    fputs(
"ais: associative index. Store things under keys, get them back by keys.\n"
"\n"
"USAGE\n"
"  ais [-f DIR] [-o] KEY...        get: records filed under ALL these keys   (default)\n"
"  ais [-f DIR] put VALUE KEY...   file VALUE under one or more keys\n"
"  ais [-f DIR] put -R DIR KEY...  file files under DIR (paths relative to a .ais root)\n"
"  ais [-f DIR] put - KEY...       file each stdin line (a value) under these keys\n"
"  ais [-f DIR] find TEXT          search: records whose value contains TEXT\n"
"  ais [-f DIR] add ID VALUE       attach another link to record ID\n"
"  ais [-f DIR] del ID             delete record ID\n"
"  ais [-f DIR] del-key KEY        delete every record filed under KEY\n"
"  ais [-f DIR] dump               print every record\n"
"  ais [-f DIR] keys               list all keys (sorted, distinct)\n"
"  ais [-f DIR] stats              counts: records, keys, deleted\n"
"  ais [-f DIR] compact            reclaim space from deleted records\n"
"  ais init                        create a local .ais index here (git-style)\n"
"  ais [-f DIR] import < FILE      add 'keys|value' lines (the inverse of dump)\n"
"  ais [-f DIR] doc KEY... < FILE  save a multi-line document as a blob file\n"
"  ais [-f DIR] where              print the resolved index directory\n"
"  ais [-f DIR] project [KEY]      show / set / clear ('') the default project key\n"
"\n"
"EXAMPLES\n"
"  # after a trip: tag one photo, then a whole folder\n"
"  ais put ~/photos/IMG_3920.jpg italy venice 2023\n"
"  ais put -R ~/photos/venice    italy venice 2023\n"
"\n"
"  # or pipe a filtered list in, tagging them all\n"
"  find ~/photos -name '*.jpg' | ais put - italy venice 2023\n"
"\n"
"  # the everyday case, just type the keys\n"
"  ais italy venice               # filed under BOTH italy AND venice\n"
"  ais -o venice rome             # under venice OR rome\n"
"\n"
"  # search by content, not tags\n"
"  ais find gelato                # any record whose value mentions gelato\n"
"\n"
"  # bulk add from an editable file (one 'keys|value' per line):\n"
"  ais import < memos.txt\n"
"\n"
"OPTIONS\n"
"  -f, --index DIR   index directory (else: see INDEX LOCATION below)\n"
"  -o, --or          match ANY key (union) instead of all (intersection)\n"
"  -p, --project KEY default project key prepended to every put ('' = none;\n"
"                    persistent via 'ais project KEY', or env $AIS_PROJECT)\n"
"  -i, --interactive add interactively: stdin lines are values, prompts for keys\n"
"  -y, --yes         assume yes for del / del-key / compact (no prompt)\n"
"  -h                short help;  --help  this full help\n"
"  -V, --version     print version and exit\n"
"\n"
"INDEX LOCATION (when -f is not given)\n"
"  1. $AIS_INDEX, if set\n"
"  2. the nearest .ais/ at or above the current directory (like git)\n"
"  3. else the per-user index $XDG_DATA_HOME/ais (else ~/.local/share/ais),\n"
"     created on first use.  Run 'ais init' to start a local .ais here.\n"
"\n"
"Re-indexing is safe: putting the same value again changes nothing (idempotent).\n"
"The index is plain text in that directory; read, grep, or repair it by hand.\n",
        out);
}
