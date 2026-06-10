hello_world -- the smallest AIS plugin.

What it does
  Prints a greeting and the index statistics (by calling `ais stats`).
  A developer-only example: copy it to start your own plugin.

Run it standalone
  AIS_INDEX=/path/to/index AIS=../../c/ais ./ais-hello

Test it in isolation (no real data touched)
  sh sandbox/run.sh

Make your own
  cp -r ../hello_world ../my_thing
  mv ../my_thing/ais-hello ../my_thing/ais-mything
  # edit ais-mything and manifest.json
