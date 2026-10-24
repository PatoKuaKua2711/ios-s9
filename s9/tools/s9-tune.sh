#!/bin/sh
# Frame-rate tuning session on the phone's iOS (kernel 0098+), nobody at the phone.
#   tools/s9-tune.sh prep              unlock + open Settings (after boot / respring)
#   tools/s9-tune.sh bench [N]         s9-bench.sh with the current settings
#   tools/s9-tune.sh capref KEY BOOL   CoreAnimation preference for backboardd
#                                      (e.g. CADisableColorMatching YES), then
#                                      restart backboardd and prep again
#   tools/s9-tune.sh membw             memory write bandwidth (dd from /dev/zero)
cd "$(dirname "$0")/.."
prep() {
	tools/s9-ui.sh unlock
	sleep 2
	tools/s9-ui.sh open com.apple.Preferences >/dev/null
	sleep 4
}
case $1 in
prep) prep ;;
bench) tools/s9-bench.sh "$(tools/s9ssh.sh 'sysctl -n kern.sweetfb_zerocopy')" "${2:-4}" | tail -4 ;;
capref)
	tools/s9ssh.sh "su mobile -c 'defaults write com.apple.backboardd $2 -bool $3' && killall -9 backboardd"
	until tools/s9ssh.sh 'test $(ps -axo etime=,comm= | grep -c "SpringBoard$") -gt 0' 2>/dev/null; do sleep 2; done
	sleep 12
	prep ;;
membw) tools/s9ssh.sh 'dd if=/dev/zero of=/dev/null bs=8m count=256 2>&1 | tail -1' ;;
*) echo "usage: s9-tune.sh prep|bench [N]|capref KEY BOOL|membw" >&2; exit 2 ;;
esac
