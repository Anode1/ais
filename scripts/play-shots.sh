#!/bin/sh
# play-shots.sh: the Play Store screenshots, from the captures in screenshots/.
#
# A capture (screenshots/android-NAME.png) is 1080x2400 with alpha, and Play
# refuses both the alpha and the 2.22 ratio. Two sets come out, each 1200x2400,
# 24-bit, no alpha:
#   screenshots/play/android-NAME.png     the capture padded to 1200 wide
#   screenshots/play/captioned/N-NAME.png a caption on the brand colour above it
# Needs ImageMagick and Roboto. The captions are the table at the end.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
cap="$root/screenshots"
in="$cap/play"
out="$in/captioned"
fonts=/usr/share/fonts/truetype/roboto/unhinted/RobotoTTF
brand='#1A0DAB'
surface='#FCF8FF'   # the app's light surface, so the pad does not show
mkdir -p "$out"

shot() { # shot N NAME TITLE SUBTITLE
    convert "$cap/android-$2.png" -background "$surface" -alpha remove \
        -alpha off -gravity center -extent 1200x2400 -strip \
        -define png:exclude-chunks=date,time "PNG24:$in/android-$2.png"
    # Captioned: crop the pad off again, scale to 900x2000, round the top
    # corners, set it under the caption.
    convert "$in/android-$2.png" -gravity center -crop 1080x2400+0+0 +repage \
        -resize 900x2000 \
        \( +clone -alpha extract -fill black -colorize 100 -fill white \
           -draw 'roundrectangle 0,0 899,2100 40,40' \) \
        -alpha off -compose CopyOpacity -composite "$out/.shot.png"
    convert -size 1200x2400 "xc:$brand" \
        -fill white -gravity north \
        -font "$fonts/Roboto-Bold.ttf" -pointsize 68 -annotate +0+110 "$3" \
        -font "$fonts/Roboto-Regular.ttf" -pointsize 44 -annotate +0+230 "$4" \
        "$out/.shot.png" -gravity south -compose over -composite \
        -alpha remove -alpha off -type TrueColor -strip \
        -define png:exclude-chunks=date,time "PNG24:$out/$1-$2.png"
    rm -f "$out/.shot.png"
}

shot 1 timeline 'Everything you want to find again' 'Links, notes, passwords. No account, no cloud.'
shot 2 add      'Save it under your own tags'       'The words you will think of later.'
shot 3 search   'Find it by those tags'             'Any combination, typed or spoken.'
shot 4 tags     'Every tag you have used'           'Plain text on your phone, yours to export.'
