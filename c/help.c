/* help.c -- AIS usage text.
 * usage_short() for a bare invocation and -h; usage_long() for --help.
 * Compiled into the binary (no runtime file dependency). */
#include "help.h"

void usage_short(FILE *out)
{
    fputs(
"usage: ais [-f DIR] [-o] KEY...        get records under ALL keys (default)\n"
"       ais [-f DIR] put VALUE KEY...   file a value under keys  (-R DIR: a folder; - : stdin)\n"
"       ais [-f DIR] add ID VALUE  |  del ID  |  dump  |  keys  |  compact\n"
"  -f DIR  index dir (default ./INDEX, or $AIS_INDEX)\n"
"  -o      match ANY key (OR) instead of ALL (AND)\n"
"  -V      print version\n"
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
"  ais [-f DIR] put -R DIR KEY...  file every file under DIR, under these keys\n"
"  ais [-f DIR] put - KEY...       file each stdin line (a value) under these keys\n"
"  ais [-f DIR] add ID VALUE       attach another link to record ID\n"
"  ais [-f DIR] del ID             delete record ID\n"
"  ais [-f DIR] dump               print every record\n"
"  ais [-f DIR] keys               list all keys (sorted, distinct)\n"
"  ais [-f DIR] compact            reclaim space from deleted records\n"
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
"OPTIONS\n"
"  -f, --index DIR   index directory (default: ./INDEX, or $AIS_INDEX)\n"
"  -o, --or          match ANY key (union) instead of all (intersection)\n"
"  -h                short help;  --help  this full help\n"
"  -V, --version     print version and exit\n"
"\n"
"Re-indexing is safe: putting the same value again changes nothing (idempotent).\n"
"The index is plain text under DIR; you can read, grep, or repair it by hand.\n",
        out);
}
