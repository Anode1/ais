#!/bin/sh
# windows-installer.sh -- GUI layer: the Windows installer (installer/ais.iss),
# run on Windows. $1 is the setup exe, built by iscc from the two release exes
# (native-windows.yml shows the call).
#
# Installs silently and checks what a user meets: ais.exe and ais-gui.exe in
# %LOCALAPPDATA%\Programs\AIS, "AIS" in the Start menu and on the Desktop, the
# folder on the user's PATH, the installed launcher starting and quitting
# (windows-launcher.sh); then uninstalls silently and checks that every one of
# those is gone.
#
# Needs Windows (Git Bash, as native-windows.yml has); elsewhere SKIP.
# Exit 0 = passed, 1 = FAIL, 77 = SKIP.

S=${1:-}
here=$(cd "$(dirname "$0")" && pwd)
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) ;;
    *) echo "  SKIP needs Windows (native-windows.yml runs it)"; exit 77 ;;
esac
[ -n "$S" ] && [ -f "$S" ] || { echo "  SKIP no setup exe at '$S' (iscc installer/ais.iss)"; exit 77; }

export MSYS_NO_PATHCONV=1      # /VERYSILENT and /v are switches, not paths
app=$(cygpath -u "$LOCALAPPDATA")/Programs/AIS
appwin=$(cygpath -w "$app")
menu=$(cygpath -u "$APPDATA")/Microsoft/Windows/Start\ Menu/Programs/AIS.lnk
desk=$(cygpath -u "$USERPROFILE")/Desktop/AIS.lnk
fail=0
check() {  # check DESCRIPTION TEST...
    d=$1; shift
    if "$@"; then echo "  ok   $d"; else echo "  FAIL $d"; fail=1; fi
}
in_path() { reg query 'HKCU\Environment' /v Path 2>/dev/null | grep -qiF "$appwin"; }
not() { ! "$@"; }

"$S" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART "/LOG=$(cygpath -w "$PWD")\\setup.log"
rc=$?
[ $rc -eq 0 ] || { echo "  FAIL setup exited $rc"; tail -20 setup.log; exit 1; }

check "ais.exe installed"        test -f "$app/ais.exe"
check "ais-gui.exe installed"    test -f "$app/ais-gui.exe"
check "README.txt installed"     test -f "$app/README.txt"
check "the uninstaller is there" test -f "$app/unins000.exe"
check "Start menu entry AIS"     test -f "$menu"
check "Desktop icon AIS"         test -f "$desk"
check "the folder is on the user's PATH" in_path
[ $fail -eq 0 ] || exit 1

sh "$here/windows-launcher.sh" "$app/ais-gui.exe" || exit 1

"$app/unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
# the uninstaller hands over to a copy of itself in temp: wait for the folder
i=0
while [ -d "$app" ] && [ $i -lt 150 ]; do sleep 0.2; i=$((i + 1)); done

check "the install folder is gone"  not test -d "$app"
check "the Start menu entry is gone" not test -f "$menu"
check "the Desktop icon is gone"     not test -f "$desk"
check "the PATH entry is gone"       not in_path
exit $fail
