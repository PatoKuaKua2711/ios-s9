#!/bin/sh
# Rehearse a phone script on the PC, with no phone: a sandbox copy of tools/
# and jb/ where tools/s9ssh.sh runs each command locally, the device paths
# (/usr/local/bin, /usr/lib/TweakInject, /var/mobile, /System/Library/Audio,
# /tmp/spin.txt) moved under SANDBOX/root, and fake sysctl (counters that grow
# like 25 fps; -w stores), ps, killall (a new SpringBoard pid after killing
# backboardd), uiopen, chown, spindump, plus stand-ins for s9top, s9membw,
# s9batt and s9play (which sends a 1 s S9Audio tone to 127.0.0.1:5141). Every
# PC-side sleep is 10x shorter. Catches flow, quoting and file-transfer bugs.
#   tools/s9-dryrun.sh SANDBOX SCRIPT [ARGS...]
#   e.g. tools/s9-dryrun.sh /tmp/dry tools/s9-measure.sh /tmp/dry/out
# Commands sent are logged in SANDBOX/state/ssh.log.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
F=${1:?usage: s9-dryrun.sh SANDBOX SCRIPT [ARGS...]}
shift
mkdir -p "$F"
F=$(cd "$F" && pwd)
rm -rf "$F/s9port" "$F/bin" "$F/stubs" "$F/state" "$F/root" "$F/fastpath"
mkdir -p "$F/s9port/jb" "$F/s9port/logs" "$F/bin" "$F/stubs" "$F/state" "$F/root/tmp" \
	"$F/root/System/Library/Audio/Plug-Ins/HAL" "$F/fastpath"
cp -a "$here/tools" "$F/s9port/"
for d in measure audio; do
	[ -d "$here/jb/$d" ] && cp -a "$here/jb/$d" "$F/s9port/jb/"
done
echo 100 > "$F/state/sbpid"

cat > "$F/s9port/tools/s9ssh.sh" <<EOF
#!/bin/sh
F=$F
cmd="\$*"
printf 'ssh: %s\n' "\$cmd" >> \$F/state/ssh.log
cmd=\$(printf '%s' "\$cmd" | sed -e "s#/usr/local/bin/\\(s9top\\|s9membw\\|s9batt\\|s9play\\) #\$F/stubs/\\1 #g" \\
	-e "s#/usr/local/bin/\\(s9top\\|s9membw\\|s9batt\\)\\\$#\$F/stubs/\\1#" \\
	-e "s#/usr/local/bin#\$F/root/usr/local/bin#g" -e "s#/usr/lib/TweakInject#\$F/root/usr/lib/TweakInject#g" \\
	-e "s#/var/mobile#\$F/root/var/mobile#g" -e "s#/tmp/spin.txt#\$F/root/tmp/spin.txt#g" \\
	-e "s#/System/Library/Audio#\$F/root/System/Library/Audio#g")
PATH="\$F/bin:\$PATH" exec sh -c "\$cmd"
EOF

cat > "$F/bin/sysctl" <<EOF
#!/bin/sh
F=$F
EOF
cat >> "$F/bin/sysctl" <<'EOF'
ms=$(( $(date +%s%N) / 1000000 ))
val() {
	case $1 in
	kern.sweetfb_swaps|kern.sweetfb_waits) echo $((ms / 40)) ;;
	kern.sweetfb_flips) echo $((ms / 41)) ;;
	kern.sweetfb_present_ns) echo $((ms / 40 * 300000)) ;;
	kern.sweetfb_wait_ns) echo $((ms / 40 * 50000)) ;;
	kern.sweet_cpu_freq) echo "l3 1478 MHz (row 27), silver 1689 MHz (row 15), gold 2649 MHz (row 23)" ;;
	kern.sweet_battery) echo "soc=87.3% vcell=4012mV avg=-312mA chgin=1 dtls=0x8 cnfg00=0x04" ;;
	*) if [ -f "$F/state/$1" ]; then cat "$F/state/$1"; else echo 0; fi ;;
	esac
}
n=0
case $1 in
-a) for k in kern.sweet_battery kern.sweet_cpu_freq kern.sweet_idle_flush; do echo "$k: $(val $k)"; done; exit 0 ;;
-w) shift; for kv in "$@"; do echo "${kv%%=*}: $(val ${kv%%=*}) -> ${kv#*=}"; echo "${kv#*=}" > "$F/state/${kv%%=*}"; done; exit 0 ;;
-n) n=1; shift ;;
esac
for k in "$@"; do
	if [ $n = 1 ]; then val "$k"; else echo "$k: $(val "$k")"; fi
done
EOF

cat > "$F/bin/ps" <<EOF
#!/bin/sh
case "\$*" in
*pid=,comm=*) echo "   50 /usr/libexec/backboardd"; echo "  \$(cat $F/state/sbpid) /System/Library/CoreServices/SpringBoard.app/SpringBoard"; echo "   70 /usr/sbin/mediaserverd" ;;
*command=*) echo "/sbin/launchd"; echo "/usr/libexec/backboardd" ;;
esac
EOF
cat > "$F/bin/killall" <<EOF
#!/bin/sh
for a in "\$@"; do [ "\$a" = backboardd ] && echo \$(( \$(cat $F/state/sbpid) + 1 )) > $F/state/sbpid; done
exit 0
EOF
printf '#!/bin/sh\nexit 0\n' > "$F/bin/uiopen"
printf '#!/bin/sh\nexit 0\n' > "$F/bin/chown"
cat > "$F/bin/spindump" <<'EOF'
#!/bin/sh
while [ $# -gt 0 ]; do [ "$1" = -file ] && { shift; printf 'Process: backboardd [50]\n' > "$1"; }; shift; done
EOF

cat > "$F/stubs/s9top" <<'EOF'
#!/bin/sh
n=10; while getopts "ai:n:t:" o; do [ $o = n ] && n=$OPTARG; done
i=0; while [ $i -lt $n ]; do
printf -- '--- 1.00 s  cpu busy %%: 10 20 30 40 | 50 60 70 80   (A55 100%%, A75 260%% of 400)\n  irq/s: 100 200 300 400 | 500 600 700 800\n  ipi/s: 10 20 30 40 | 50 60 70 80\n  timer/s: 1 2 3 4 | 5 6 7 8\n  top processes:\n     85.0%%  backboardd           [50]  csw/s 1000  syscalls/s 2000\n  backboardd [50]: 85%% of one core, 20 threads\n     70.0%%  pri 47/47  0x1234 com.apple.coreanimation.render-server\n'
i=$((i + 1)); done
EOF
cat > "$F/stubs/s9membw" <<'EOF'
#!/bin/sh
printf 'buffers: 8 MB, 32 passes per test (GB/s; copy counts read + write)\nthreads   write    copy    read\n  1       9.10    6.20   11.00\nlatency (32 MB random chain): 110.0 ns per load (end 0x1)\n'
EOF
cat > "$F/stubs/s9batt" <<'EOF'
#!/bin/sh
echo "1. kernel"
echo "    kern.sweet_battery: soc=87.3% vcell=4012mV avg=-312mA"
EOF
cat > "$F/stubs/s9play" <<'EOF'
#!/usr/bin/env python3
import math, socket, struct, time
print('route: 1 output(s)\n  output: type USBAudio, name "S9 Audio (PC)", UID S9Audio_Output')
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
seq, phase = 0, 0.0
for _ in range(48000 // 340 + 1):
    pcm = b''
    for i in range(340):
        v = int(math.sin(phase) * 8000)
        phase += 2 * math.pi * 440 / 48000
        pcm += struct.pack('<hh', v, v)
    s.sendto(struct.pack('<IIII', 0x55413953, seq, 48000, 340 | (2 << 16) | (16 << 24)) + pcm, ('127.0.0.1', 5141))
    seq += 1
    time.sleep(340 / 48000)
EOF
cat > "$F/fastpath/sleep" <<'EOF'
#!/bin/sh
exec /usr/bin/sleep "$(python3 -c "import sys; print(float(sys.argv[1]) / 10)" "$1")"
EOF
chmod 755 "$F/s9port/tools/s9ssh.sh" "$F"/bin/* "$F"/stubs/* "$F/fastpath/sleep"

cd "$F/s9port"
script=$1
shift
PATH="$F/fastpath:$PATH" sh "$script" "$@"
echo "dry run: commands in $F/state/ssh.log, phone files under $F/root" >&2
