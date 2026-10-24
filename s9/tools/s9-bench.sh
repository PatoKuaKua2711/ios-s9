#!/bin/sh
# Frame-rate benchmark on the phone's iOS (kernel 0096+), nobody at the phone:
# picks the presentation path, plays N synthetic flings (sysctl
# kern.sweet_swipe) on whatever is on screen (open a list first, e.g.
# Settings) and samples the framebuffer counters about every 0.1 s.
#   tools/s9-bench.sh ZEROCOPY(0|1) [N=6]
# Prints frames per second for each second with activity, then a summary:
# swaps = frames CoreAnimation finished, shown = flips (zero-copy) + copies.
cd "$(dirname "$0")/.."
zc=${1:?usage: s9-bench.sh 0|1 [N]}
n=${2:-6}
k="kern.sweetfb_swaps kern.sweetfb_flips kern.sweetfb_copies kern.sweetfb_present_ns kern.sweetfb_waits kern.sweetfb_wait_ns kern.sweetfb_wait_timeouts"
tools/s9ssh.sh "sysctl -w kern.sweetfb_zerocopy=$zc >/dev/null; sleep 2; sysctl -w kern.sweet_swipe=$n >/dev/null;
	i=0; while [ \$i -lt $((n * 36 + 10)) ]; do echo \$(date +%s) \$(sysctl -n $k); sleep 0.1; i=\$((i + 1)); done" |
python3 -c '
import sys
rows = [list(map(int, l.split())) for l in sys.stdin if len(l.split()) == 8]
if not rows:
    sys.exit("no samples (kernel without kern.sweetfb_* sysctls?)")
by = {}
for r in rows:
    by.setdefault(r[0], []).append(r)
secs = sorted(by)
fps = []
for a, b in zip(secs, secs[1:]):
    d = [y - x for x, y in zip(by[a][-1], by[b][-1])]
    if d[1] > 2:
        fps.append((d[1], d[2] + d[3], d[4] / max(1, d[2] + d[3]) / 1000))
first, last = rows[0], rows[-1]
d = [y - x for x, y in zip(first, last)]
print("path:", "zero-copy" if sys.argv[1] == "1" else "CPU copy")
print("per active second: swaps/s, shown/s, present us/frame")
for f in fps:
    print("  %3d %3d %6.0f" % f)
if fps:
    s = sorted(f[0] for f in fps)
    print("swaps/s: median %d, max %d, over %d active seconds" % (s[len(s) // 2], s[-1], len(s)))
shown = d[2] + d[3]
print("total: %d swaps, %d flips, %d copies, present %.0f us/frame, SwapWait %d calls avg %.0f us, %d timeouts" % (
    d[1], d[2], d[3], d[4] / max(1, shown) / 1000, d[5], d[6] / max(1, d[5]) / 1000, d[7]))
' "$zc"
