/* exif.c -- minimal, dependency-free EXIF reader for JPEG photos.
 *
 * Pulls only the two fields that matter for filing a photo in an index:
 *   - capture date (DateTimeOriginal, falling back to DateTime), as YYYY-MM-DD
 *   - GPS position, as decimal "latitude longitude"
 * EXIF is a TIFF block carried in the JPEG APP1 segment, so no libexif and no
 * ImageMagick are needed: the few tags we want are a short, bounded walk.
 *
 * Input is untrusted, so every read is bounds-checked against the buffer and
 * the IFD recursion is depth-limited.
 *
 *   make                 (or: cc -O2 -std=c99 -W -Wall -o exif exif.c)
 *   exif photo.jpg
 *       date<TAB>2023-07-14
 *       gps<TAB>45.440833 12.315556
 * Absent fields print nothing. Exit status: 0 if any field was found, 1 if the
 * file has none, 2 on a usage or read error. Feed it into an index, e.g.
 *   d=$(exif p.jpg | sed -n 's/^date\t//p'); ais -v p.jpg "$d" "${d%%-*}"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREFIX_MAX (256 * 1024)   /* EXIF rides the first APP1, <=64KB in: a safe prefix */

static int g_le;                  /* TIFF byte order: 1 = little-endian (II), 0 = big (MM) */

struct exif {
    char   date[11];              /* "YYYY-MM-DD", or "" */
    int    date_original;         /* set once DateTimeOriginal (not plain DateTime) was taken */
    int    have_gps;
    double lat, lon;
};

/* byte-order-aware reads; callers bounds-check the offset first */
static unsigned rd16(const unsigned char *p)
{
    return g_le ? (unsigned)p[0] | (unsigned)p[1] << 8
                : (unsigned)p[0] << 8 | (unsigned)p[1];
}
static unsigned long rd32(const unsigned char *p)
{
    return g_le ? (unsigned long)p[0] | (unsigned long)p[1] << 8
                  | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24
                : (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16
                  | (unsigned long)p[2] << 8 | (unsigned long)p[3];
}

/* a RATIONAL is two LONGs: numerator, denominator */
static double ratio(const unsigned char *p)
{
    unsigned long num = rd32(p), den = rd32(p + 4);
    return den ? (double)num / (double)den : 0.0;
}

/* GPSLatitude / GPSLongitude: three RATIONALs (deg, min, sec) -> decimal degrees */
static int gps_triplet(const unsigned char *tiff, unsigned long len, unsigned long off, double *deg)
{
    if (off > len || len - off < 24)
        return 0;
    *deg = ratio(tiff + off) + ratio(tiff + off + 8) / 60.0 + ratio(tiff + off + 16) / 3600.0;
    return 1;
}

/* the GPS sub-IFD: its tag numbers are GPS-specific, so it gets its own reader */
static void parse_gps(const unsigned char *tiff, unsigned long len, unsigned long off, struct exif *e)
{
    unsigned n, i;
    char latref = 'N', lonref = 'E';
    double lat = 0, lon = 0;
    int have_lat = 0, have_lon = 0;
    if (off > len || len - off < 2)
        return;
    n = rd16(tiff + off);
    for (i = 0; i < n; i++) {
        unsigned long ent = off + 2 + (unsigned long)i * 12;
        unsigned tag;
        if (ent > len || len - ent < 12)
            break;
        tag = rd16(tiff + ent);
        switch (tag) {
        case 0x0001: latref = (char)tiff[ent + 8]; break;                       /* N / S */
        case 0x0003: lonref = (char)tiff[ent + 8]; break;                       /* E / W */
        case 0x0002: have_lat = gps_triplet(tiff, len, rd32(tiff + ent + 8), &lat); break;
        case 0x0004: have_lon = gps_triplet(tiff, len, rd32(tiff + ent + 8), &lon); break;
        default: break;
        }
    }
    if (have_lat && have_lon) {
        e->have_gps = 1;
        e->lat = (latref == 'S') ? -lat : lat;
        e->lon = (lonref == 'W') ? -lon : lon;
    }
}

/* copy an EXIF datetime "YYYY:MM:DD hh:mm:ss" into e->date as "YYYY-MM-DD" */
static void take_date(const unsigned char *tiff, unsigned long len, unsigned long off,
                      unsigned long cnt, int original, struct exif *e)
{
    if (!original && e->date[0])                 /* DateTimeOriginal wins over DateTime */
        return;
    if (cnt < 10 || off > len || len - off < 10)
        return;
    if (tiff[off + 4] != ':' || tiff[off + 7] != ':')
        return;
    e->date[0] = tiff[off]; e->date[1] = tiff[off + 1];
    e->date[2] = tiff[off + 2]; e->date[3] = tiff[off + 3];
    e->date[4] = '-';
    e->date[5] = tiff[off + 5]; e->date[6] = tiff[off + 6];
    e->date[7] = '-';
    e->date[8] = tiff[off + 8]; e->date[9] = tiff[off + 9];
    e->date[10] = '\0';
    if (original)
        e->date_original = 1;
}

/* walk one IFD; recurse into the Exif (0x8769) and GPS (0x8825) sub-IFDs */
static void parse_ifd(const unsigned char *tiff, unsigned long len, unsigned long off,
                      struct exif *e, int depth)
{
    static const unsigned char tsize[] = { 0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8 };
    unsigned n, i;
    if (depth > 2 || off > len || len - off < 2)
        return;
    n = rd16(tiff + off);
    for (i = 0; i < n; i++) {
        unsigned long ent = off + 2 + (unsigned long)i * 12;
        unsigned tag, type, cnt;
        unsigned long dataoff;
        unsigned esz;
        if (ent > len || len - ent < 12)
            break;
        tag  = rd16(tiff + ent);
        type = rd16(tiff + ent + 2);
        cnt  = (unsigned)rd32(tiff + ent + 4);
        esz  = (type < sizeof tsize) ? tsize[type] : 0;
        /* the value sits inline in the 4-byte field, or at the offset it holds */
        dataoff = ((unsigned long long)esz * cnt <= 4) ? ent + 8 : rd32(tiff + ent + 8);
        switch (tag) {
        case 0x0132: take_date(tiff, len, dataoff, cnt, 0, e); break;          /* DateTime */
        case 0x9003: take_date(tiff, len, dataoff, cnt, 1, e); break;          /* DateTimeOriginal */
        case 0x8769: parse_ifd(tiff, len, rd32(tiff + ent + 8), e, depth + 1); break; /* Exif IFD */
        case 0x8825: parse_gps(tiff, len, rd32(tiff + ent + 8), e); break;     /* GPS IFD */
        default: break;
        }
    }
}

int main(int argc, char **argv)
{
    FILE *f;
    unsigned char *buf;
    size_t len, p;
    struct exif e;
    int found = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: exif FILE.jpg\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (f == NULL) {
        fprintf(stderr, "exif: cannot open %s\n", argv[1]);
        return 2;
    }
    buf = malloc(PREFIX_MAX);
    if (buf == NULL) {
        fclose(f);
        return 2;
    }
    len = fread(buf, 1, PREFIX_MAX, f);
    fclose(f);

    memset(&e, 0, sizeof e);
    if (len < 4 || buf[0] != 0xFF || buf[1] != 0xD8) {     /* SOI */
        fprintf(stderr, "exif: not a JPEG: %s\n", argv[1]);
        free(buf);
        return 2;
    }
    /* walk JPEG segments to the APP1 that begins "Exif\0\0" */
    p = 2;
    while (p + 4 <= len) {
        unsigned marker, seglen;
        if (buf[p] != 0xFF) { p++; continue; }
        while (p + 1 < len && buf[p + 1] == 0xFF) p++;      /* skip fill bytes */
        if (p + 4 > len) break;
        marker = buf[p + 1];
        if (marker == 0xD9 || marker == 0xDA)               /* EOI / start of scan */
            break;
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) { p += 2; continue; }  /* no length */
        seglen = (unsigned)(buf[p + 2] << 8 | buf[p + 3]);
        if (seglen < 2 || p + 2 + seglen > len)
            break;
        if (marker == 0xE1 && seglen >= 8 && memcmp(buf + p + 4, "Exif\0\0", 6) == 0) {
            const unsigned char *tiff = buf + p + 10;
            unsigned long tlen = seglen - 8;
            if (tlen >= 8 && ((tiff[0] == 'I' && tiff[1] == 'I') ||
                              (tiff[0] == 'M' && tiff[1] == 'M'))) {
                g_le = (tiff[0] == 'I');
                if (rd16(tiff + 2) == 42)
                    parse_ifd(tiff, tlen, rd32(tiff + 4), &e, 0);
            }
            break;                                          /* the first Exif APP1 is the one */
        }
        p += 2 + seglen;
    }

    if (e.date[0]) { printf("date\t%s\n", e.date); found = 1; }
    if (e.have_gps) { printf("gps\t%.6f %.6f\n", e.lat, e.lon); found = 1; }
    free(buf);
    return found ? 0 : 1;
}
