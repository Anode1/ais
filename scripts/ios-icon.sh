#!/bin/sh
# ios-icon.sh: render the iOS app icon set from the AIS artwork.
#
# Source: app/flutter/assets/icon/ais.png, the same 512 px image the Android
# launcher icon is cut from. It has transparent rounded corners; iOS wants an
# opaque square and applies its own mask, so the corners are filled with the
# artwork's blue (the Android adaptive-icon background, colors.xml). Every slot
# in the asset catalog's Contents.json is written at size x scale pixels; the
# 1024 px marketing icon is an upscale of the 512 px source.
#
# Run after changing the artwork, then commit the appiconset. The icon ships
# inside the build, so a store listing shows it only after a new upload.
set -e
cd "$(dirname "$0")/.."
src=app/flutter/assets/icon/ais.png
set=app/flutter/ios/Runner/Assets.xcassets/AppIcon.appiconset
python3 - "$set/Contents.json" <<'PY' | sort -u | while read -r px name; do
import json, sys
for i in json.load(open(sys.argv[1]))["images"]:
    w = float(i["size"].split("x")[0]) * int(i["scale"].rstrip("x"))
    print(int(round(w)), i["filename"])
PY
    convert "$src" -background '#1A0DAB' -alpha remove -alpha off \
        -filter Lanczos -resize "${px}x${px}!" -type TrueColor -strip \
        -define png:exclude-chunks=date,time "PNG24:$set/$name"
    echo "$name ${px}px"
done
