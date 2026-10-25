#!/bin/sh
# One unattended measuring trip on the phone's iOS (kernel 0099+), nobody at
# the phone. Installs the read-only tools (s9top, s9membw, s9batt from
# jb/measure, built by the fork's s9-ios-tools CI) and the S9CAEnv tweak
# (backboardd; does nothing without its env.txt), then measures, cheapest and
# safest first, everything into OUT (default logs/measure-DATE):
#   1. sysctl snapshot + battery chain (s9batt)                  ~5 s
#   2. memory bandwidth and latency (s9membw)                    ~20 s
#   3. idle in Settings: CPU per thread, kernel fps line         ~25 s
#   4. Settings scrolling: s9-bench + s9top side by side         ~30 s
#   5. kern.sweet_idle_flush A/B/A (0, 1, 0)                     ~1.5 min
#   5b. dispatch_apply width, kern.sweet_parallelism 1, 2, 4       ~1 min
#   6. spindump of the scrolling (s9-profile.sh)                 ~1 min
#   7. CoreAnimation's own counters (CA_PRINT_PERF)              ~1.5 min, restarts backboardd
#   7b. CoreAnimation paced by the kernel's vsync (CA_FORCE_TIMER=1, which sets use_vbl)  ~1.5 min
#   8. who animates while idle (CA_PRINT_ANIMATIONS)             ~1 min, restarts backboardd
#   9. back to normal (env.txt removed, restart), battery again
# Never touches charging, CPU/L3/DDR clocks, the bandwidth vote or the boot
# partition. Every step that restarts backboardd checks that SpringBoard
# came back and, if not, removes env.txt and restarts again.
#   tools/s9-measure.sh [OUT]          the whole trip
#   tools/s9-measure.sh install        only install the tools and the tweak
#   tools/s9-measure.sh caenv off|KEY=VALUE...   set backboardd's CA_* and restart it
cd "$(dirname "$0")/.."
ssh9() { tools/s9ssh.sh "$@"; }
B=/usr/local/bin
CAD=/var/mobile/Library/S9CAEnv

# New file in the target's own directory, then rename: never rewrite a file
# a process has mapped (NOTES: dylib overwritten in place -> CS_INVALID).
put() {
	d=$(dirname "$2")
	b=$(basename "$2")
	ssh9 "mkdir -p '$d' && cat > '$d/.$b.new' && chmod $3 '$d/.$b.new' && mv -f '$d/.$b.new' '$2'" < "$1"
}

install_tools() {
	(cd jb/measure && sha256sum -c --quiet SHA256SUMS) || { echo "jb/measure: checksum mismatch" >&2; exit 1; }
	for t in s9top s9membw s9batt; do
		put jb/measure/$t $B/$t 755 || exit 1
	done
	put jb/measure/S9CAEnv.dylib /usr/lib/TweakInject/S9CAEnv.dylib 755 || exit 1
	put jb/measure/S9CAEnv.plist /usr/lib/TweakInject/S9CAEnv.plist 644 || exit 1
	ssh9 "mkdir -p $CAD && rm -f $CAD/env.txt && chown 501:501 $CAD && ls -la $B/s9top $B/s9membw $B/s9batt /usr/lib/TweakInject/S9CAEnv.*"
}

prep() {
	tools/s9-ui.sh unlock
	sleep 2
	tools/s9-ui.sh open com.apple.Preferences >/dev/null
	sleep 5
}

sbpid() {
	ssh9 'ps -axo pid=,comm=' 2>/dev/null | awk '$2 ~ /SpringBoard$/ { print $1; exit }'
}

# Kill backboardd (SpringBoard goes with it), wait for a new SpringBoard, prep.
restart_ui() {
	old=$(sbpid)
	ssh9 'killall -9 backboardd' || true
	i=0
	while :; do
		sleep 3
		i=$((i + 3))
		new=$(sbpid)
		[ -n "$new" ] && [ "$new" != "$old" ] && break
		[ $i -lt 150 ] || { echo "SpringBoard did not come back in 150 s" >&2; return 1; }
	done
	sleep 15
	prep
}

caenv() {
	if [ "$1" = off ]; then
		ssh9 "rm -f $CAD/env.txt"
	else
		printf '%s\n' "$@" | ssh9 "cat > $CAD/env.txt && chown 501:501 $CAD/env.txt"
	fi
	if ! restart_ui; then
		echo "backboardd/SpringBoard trouble: removing env.txt and restarting" >&2
		ssh9 "rm -f $CAD/env.txt"
		restart_ui
		return 1
	fi
}

snap() {
	ssh9 'sysctl kern.sweetfb_swaps kern.sweetfb_flips kern.sweetfb_copies kern.sweetfb_underruns
		kern.sweet_surface_locks kern.sweet_surface_creates kern.sweet_surface_create_bytes
		kern.sweet_idle_flush kern.sweet_parallelism kern.sweetfast kern.sweet_mem_vote kern.sweet_battery' > "$out/snap-$1.txt" 2>&1
}

# bench NAME [N]: N synthetic flings in Settings with s9top running beside it.
bench() {
	snap "$1-before"
	ssh9 "$B/s9top -i 1 -n $((${2:-4} * 4 + 2)) -t 10 backboardd Preferences SpringBoard" > "$out/top-$1.txt" 2>&1 &
	tp=$!
	tools/s9-bench.sh 1 "${2:-4}" > "$out/bench-$1.txt" 2>&1
	wait $tp
	snap "$1-after"
	echo "$1: $(grep 'swaps/s' "$out/bench-$1.txt")"
}

case $1 in
install) install_tools; exit ;;
caenv) shift; caenv "$@"; exit ;;
esac

out=${1:-logs/measure-$(date +%Y%m%d-%H%M)}
mkdir -p "$out"
ssh9 true || { echo "no SSH to the phone (USB network up?)" >&2; exit 1; }
python3 -I tools/s9-log.py "$out/kernel.txt" &
logp=$!
trap 'kill $logp 2>/dev/null' EXIT

echo "== install"
install_tools > "$out/install.txt" 2>&1 || { cat "$out/install.txt"; exit 1; }

echo "== 1. snapshot + battery"
ssh9 'uname -a; sysctl -a | grep -i sweet' > "$out/sysctl.txt" 2>&1
ssh9 "$B/s9batt -a" > "$out/batt-start.txt" 2>&1

echo "== 2. memory"
ssh9 "$B/s9membw" > "$out/membw.txt" 2>&1
cat "$out/membw.txt"

echo "== 3. idle in Settings"
prep
snap idle-before
ssh9 "$B/s9top -i 2 -n 10 -t 8 backboardd SpringBoard Preferences kernel_task" > "$out/top-idle.txt" 2>&1
snap idle-after

echo "== 4. scrolling, defaults"
bench base 4

echo "== 5. idle flush A/B/A"
ssh9 'sysctl -w kern.sweet_idle_flush=1' > /dev/null && bench flush1 4
ssh9 'sysctl -w kern.sweet_idle_flush=0' > /dev/null && bench flush0 4

echo "== 5b. dispatch_apply width (kern.sweet_parallelism 1, 2, 4; 0 = every core, the default)"
# Every polygon asks the kernel for up to N-1 helper threads (a syscall,
# wake-ups and IPIs), even a small one that the caller finishes alone: fewer
# helpers may cost less than they save. 1 = no helpers at all.
for p in 1 2 4; do
	ssh9 "sysctl -w kern.sweet_parallelism=$p" > /dev/null && bench par$p 3
done
ssh9 'sysctl -w kern.sweet_parallelism=0' > /dev/null

echo "== 6. spindump while scrolling"
tools/s9-profile.sh "$out/prof-scroll" 8 > "$out/prof.txt" 2>&1

echo "== 7. CoreAnimation counters (CA_PRINT_PERF)"
if caenv CA_PRINT_PERF=1 X_LOG_FILE=$CAD/ca-perf.log; then
	bench caperf 4
	sleep 10
	ssh9 "cat $CAD/ca-perf.log" > "$out/ca-perf.log" 2>&1
fi

echo "== 7b. frames paced by the kernel's vsync (CA_FORCE_TIMER=1 = use_vbl, sel 72 notifications)"
if caenv CA_FORCE_TIMER=1 CA_PRINT_PERF=1 X_LOG_FILE=$CAD/ca-vbl.log; then
	bench vbl 4
	ssh9 "cat $CAD/ca-vbl.log" > "$out/ca-vbl.log" 2>&1
fi

echo "== 8. animations while idle (CA_PRINT_ANIMATIONS)"
if caenv CA_PRINT_ANIMATIONS=1 X_LOG_FILE=$CAD/ca-anim.log; then
	sleep 20
	ssh9 "cat $CAD/ca-anim.log" > "$out/ca-anim.log" 2>&1
fi

echo "== 9. back to normal"
caenv off
ssh9 "cat $CAD/log.txt" > "$out/caenv-log.txt" 2>&1
ssh9 "$B/s9batt" > "$out/batt-end.txt" 2>&1
grep -h "kern.sweet_battery" "$out/batt-start.txt" "$out/batt-end.txt"
grep -a "present avg" "$out/kernel.txt" | tail -40 > "$out/fps-lines.txt"
ls -la "$out"
