# util: archive-maintenance helpers

Small tools the index keeper runs to groom a photo or file archive. They are
NOT part of the engine: the `ais` binary never depends on them, and they may
shell out to heavier tools that only the maintainer needs.

## Files
- `exif.c`: dependency-free EXIF reader. Prints a JPEG's capture date and GPS.
- `exif-tag.sh`: walk a directory and file each JPEG into an index by date + year.
- `mktest.c`: emits a known-answer EXIF JPEG, used only by the test.
- `test.sh`: known-answer check (run by `make check`).

## Build and test
    make            # builds ./exif
    make check      # builds the fixture and asserts the reader decodes it

## Use
    ./exif photo.jpg
    ./exif-tag.sh -f ~/.ais-photos ~/photos italy
