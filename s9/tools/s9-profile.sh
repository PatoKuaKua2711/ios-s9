#!/bin/sh
# Where CoreAnimation's time goes while scrolling: spindump on the phone during
# synthetic flings (kernel 0097+), symbolized on the PC.
#   tools/s9-profile.sh OUT_PREFIX [SECONDS=8]
# Leaves OUT_PREFIX.raw.txt (spindump) and OUT_PREFIX.sym.txt (backboardd only).
cd "$(dirname "$0")/.."
out=${1:?usage: s9-profile.sh OUT_PREFIX [SECONDS]}
secs=${2:-8}
n=$(( (secs + 3) / 3 ))
tools/s9ssh.sh "rm -f /tmp/spin.txt; sysctl -w kern.sweet_swipe=$n >/dev/null; sleep 0.5;
	spindump -notarget $secs 10 -file /tmp/spin.txt >/dev/null 2>&1; ls -la /tmp/spin.txt"
tools/s9ssh.sh "cat /tmp/spin.txt" > "$out.raw.txt"
DSC=../work/dsc/dyld_shared_cache_arm64 python3 -I tools/spin-sym.py "$out.raw.txt" backboardd > "$out.sym.txt"
wc -l "$out.raw.txt" "$out.sym.txt"
