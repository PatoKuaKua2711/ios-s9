#!/bin/sh
# Drive the phone's iOS UI from the PC with the kernel's synthetic finger
# (0097+: sysctl kern.sweet_gesture "x0 y0 x1 y1 ms", panel pixels 1440x2960).
#   tools/s9-ui.sh unlock            swipe up from the bottom edge
#   tools/s9-ui.sh home              same gesture (home from an app)
#   tools/s9-ui.sh tap X Y           tap in panel pixels
#   tools/s9-ui.sh swipe X0 Y0 X1 Y1 MS
#   tools/s9-ui.sh open BUNDLEID     open an app (uiopen), e.g. com.apple.Preferences
cd "$(dirname "$0")/.."
g() { tools/s9ssh.sh "sysctl -w kern.sweet_gesture='$*' >/dev/null"; }
case $1 in
unlock|home) g 720 2950 720 1500 150 ;;
tap) g "$2" "$3" "$2" "$3" 0 ;;
swipe) g "$2" "$3" "$4" "$5" "$6" ;;
open) tools/s9ssh.sh "uiopen --bundleid $2 || uiopen -b $2" ;;
*) echo "usage: s9-ui.sh unlock|home|tap X Y|swipe X0 Y0 X1 Y1 MS|open BUNDLEID" >&2; exit 2 ;;
esac
