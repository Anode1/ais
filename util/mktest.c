/* mktest.c -- emit a known-answer EXIF JPEG on stdout, for util/test.sh.
 *
 * Little-endian TIFF: IFD0 (DateTime, ExifIFD pointer, GPS pointer), an Exif
 * sub-IFD holding DateTimeOriginal, and a GPS sub-IFD = 45 26 27 N, 12 18 56 E.
 * The reader must therefore report:
 *     date  2023-07-14    (DateTimeOriginal wins over the 2021 DateTime)
 *     gps   45.440833 12.315556
 * The encoder is kept independent of exif.c on purpose, so the test is a real
 * cross-check, not the parser grading its own output.
 */
#include <stdio.h>
#include <string.h>

static unsigned char t[210];                 /* the TIFF block (offsets are fixed below) */

static void w16(int o, unsigned v)      { t[o] = v & 0xFF; t[o + 1] = v >> 8 & 0xFF; }
static void w32(int o, unsigned long v) { t[o] = v & 0xFF; t[o + 1] = v >> 8 & 0xFF;
                                          t[o + 2] = v >> 16 & 0xFF; t[o + 3] = v >> 24 & 0xFF; }
static void ent(int o, unsigned tag, unsigned typ, unsigned long cnt, unsigned long val)
{
    w16(o, tag); w16(o + 2, typ); w32(o + 4, cnt); w32(o + 8, val);
}

int main(void)
{
    unsigned char head[12], eoi[2] = { 0xFF, 0xD9 };
    unsigned seg = 2 + 6 + 210;              /* APP1 length: len field + "Exif\0\0" + TIFF */

    t[0] = 'I'; t[1] = 'I'; w16(2, 42); w32(4, 8);            /* TIFF header, IFD0 at 8 */
    w16(8, 3);                                                /* IFD0: 3 entries */
    ent(10, 0x0132, 2, 20, 122); ent(22, 0x8769, 4, 1, 50); ent(34, 0x8825, 4, 1, 68); w32(46, 0);
    w16(50, 1); ent(52, 0x9003, 2, 20, 142); w32(64, 0);     /* Exif sub-IFD: DateTimeOriginal */
    w16(68, 4);                                              /* GPS sub-IFD: 4 entries */
    ent(70, 0x0001, 2, 2, 'N'); ent(82, 0x0002, 5, 3, 162);
    ent(94, 0x0003, 2, 2, 'E'); ent(106, 0x0004, 5, 3, 186); w32(118, 0);
    memcpy(t + 122, "2021:01:02 03:04:05", 20);              /* DateTime (IFD0) */
    memcpy(t + 142, "2023:07:14 09:30:00", 20);              /* DateTimeOriginal */
    w32(162, 45); w32(166, 1); w32(170, 26); w32(174, 1); w32(178, 27); w32(182, 1);  /* lat */
    w32(186, 12); w32(190, 1); w32(194, 18); w32(198, 1); w32(202, 56); w32(206, 1);  /* lon */

    head[0] = 0xFF; head[1] = 0xD8;                          /* SOI */
    head[2] = 0xFF; head[3] = 0xE1;                          /* APP1 */
    head[4] = seg >> 8 & 0xFF; head[5] = seg & 0xFF;
    head[6] = 'E'; head[7] = 'x'; head[8] = 'i'; head[9] = 'f'; head[10] = 0; head[11] = 0;
    fwrite(head, 1, 12, stdout);
    fwrite(t, 1, 210, stdout);
    fwrite(eoi, 1, 2, stdout);
    return 0;
}
