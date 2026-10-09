#!/bin/sh
# appstore-shots.sh: the App Store screenshots from the captures.
# The captures are screenshots/iphone_*.png from an iPhone 11 (828x1792). App
# Store Connect takes no capture at its native size: the one iPhone slot it
# offers is "iPhone with Dynamic Island (medium display)", 1179x2556 or
# 1206x2622, 24-bit, no alpha, and any other size is refused as wrong
# dimensions. Each capture is resized to 1206x2622 (the aspect ratios differ
# by 0.5%, so the stretch is invisible) with nothing added, status bar kept.
# Output: fastlane/metadata/ios/en-US/images/phoneScreenshots/N.png.
# Needs ImageMagick.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
cap="$root/screenshots"
out="$root/fastlane/metadata/ios/en-US/images/phoneScreenshots"
mkdir -p "$out"

shot() { # shot N NAME
    convert "$cap/iphone_$2.png" -depth 8 -resize '1206x2622!' \
        -alpha remove -alpha off -type TrueColor -strip \
        -define png:exclude-chunks=date,time "PNG24:$out/$1.png"
}

shot 1 timeline
shot 2 add
shot 3 search
shot 4 encrypt
