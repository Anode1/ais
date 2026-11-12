# android-apk.sh -- sourced by the Android layers: build the APK once per run.
#
# Each layer ran its own `flutter build apk`, about a minute of Gradle for a
# file the layer before it had just built. tests/run.sh exports AIS_APK_RUN,
# one value per suite run; a layer that finds the APK stamped with that value
# installs it as it is. Run alone (no AIS_APK_RUN) a layer always builds, so a
# stale APK cannot pass for the current source.
#
# apk_build APPDIR MODE TARGET-PLATFORM LOGFILE
#   0 with the APK at APPDIR/build/app/outputs/flutter-apk/app-MODE.apk, 1 on a
#   failed build (the log says why).
apk_build() {
    _out="$1/build/app/outputs/flutter-apk/app-$2.apk"
    _stamp="$1/build/.ais_apk_$2_$3"
    if [ -n "${AIS_APK_RUN:-}" ] && [ -f "$_out" ] &&
       [ "$(cat "$_stamp" 2>/dev/null)" = "$AIS_APK_RUN" ]; then
        echo "reused the APK built earlier in this run" >"$4"
        return 0
    fi
    # app-MODE.apk is one file whatever the target platform: a build for one
    # platform voids the stamp of every other
    rm -f "$1/build/.ais_apk_$2_"*
    ( cd "$1" && flutter build apk "--$2" --target-platform "$3" ) >"$4" 2>&1 || return 1
    [ -f "$_out" ] || return 1
    [ -z "${AIS_APK_RUN:-}" ] || echo "$AIS_APK_RUN" >"$_stamp"
    return 0
}
