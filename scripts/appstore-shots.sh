#!/bin/sh
# appstore-shots.sh: the App Store screenshots, from the same captures as
# play-shots.sh. App Store Connect wants one set at the 6.9-inch iPhone size,
# 1320x2868, 24-bit, no alpha. The Android status bar (top 95 px of a capture)
# and the gesture bar (bottom 60 px) are cropped off, so the frame shows the
# app alone. Output: fastlane/metadata/ios/en-US/images/phoneScreenshots/N.png.
# Needs ImageMagick and Roboto. The captions are the table at the end.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
cap="$root/screenshots"
out="$root/fastlane/metadata/ios/en-US/images/phoneScreenshots"
fonts=/usr/share/fonts/truetype/roboto/unhinted/RobotoTTF
brand='#1A0DAB'
mkdir -p "$out"

shot() { # shot N NAME TITLE SUBTITLE
    convert "$cap/android-$2.png" -crop 1080x2245+0+95 +repage \
        -resize 1140x2370 \
        \( +clone -alpha extract -fill black -colorize 100 -fill white \
           -draw 'roundrectangle 0,0 1139,2500 48,48' \) \
        -alpha off -compose CopyOpacity -composite "$out/.shot.png"
    convert -size 1320x2868 "xc:$brand" \
        -fill white -gravity north \
        -font "$fonts/Roboto-Bold.ttf" -pointsize 76 -annotate +0+150 "$3" \
        -font "$fonts/Roboto-Regular.ttf" -pointsize 50 -annotate +0+290 "$4" \
        "$out/.shot.png" -gravity south -compose over -composite \
        -alpha remove -alpha off -type TrueColor -strip \
        -define png:exclude-chunks=date,time "PNG24:$out/$1.png"
    rm -f "$out/.shot.png"
}

shot 1 timeline 'Everything you want to find again' 'Links, notes, passwords. No account, no cloud.'
shot 2 add      'Save it under your own tags'       'The words you will think of later.'
shot 3 search   'Find it by those tags'             'Any combination, typed or spoken.'
shot 4 tags     'Every tag you have used'           'Plain text on your phone, yours to export.'
