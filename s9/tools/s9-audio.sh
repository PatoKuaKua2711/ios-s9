#!/bin/sh
# Audio path A on the phone's iOS (s9port study, section 3): the S9Audio HAL
# driver (jb/audio/S9Audio.driver, fork CI s9-ios-tools) makes iOS send its
# sound over the USB network to the PC. Separate from tools/s9-measure.sh.
#   tools/s9-audio.sh install     driver into /System/Library/Audio/Plug-Ins/HAL, s9play, restart mediaserverd
#   tools/s9-audio.sh test [SEC]  record on the PC while s9play plays a tone (default 4 s): logs/audio-DATE.wav
#   tools/s9-audio.sh remove      take the driver out again, restart mediaserverd
cd "$(dirname "$0")/.."
HAL=/System/Library/Audio/Plug-Ins/HAL
ssh9() { tools/s9ssh.sh "$@"; }

restart_mediaserverd() {
	ssh9 'killall -9 mediaserverd 2>/dev/null; sleep 4; ps -axo pid=,comm= | grep mediaserverd || echo "mediaserverd not running"'
}

case $1 in
install)
	(cd jb/audio && sha256sum -c --quiet SHA256SUMS) || { echo "jb/audio: checksum mismatch" >&2; exit 1; }
	# The bundle goes in beside its final place and is renamed in: no file a process maps is rewritten.
	tar -C jb/audio -cf - S9Audio.driver | ssh9 "rm -rf $HAL/.S9Audio.new && mkdir $HAL/.S9Audio.new &&
		tar -C $HAL/.S9Audio.new -xf - && rm -rf $HAL/.S9Audio.old &&
		{ [ ! -e $HAL/S9Audio.driver ] || mv $HAL/S9Audio.driver $HAL/.S9Audio.old; } &&
		mv $HAL/.S9Audio.new/S9Audio.driver $HAL/S9Audio.driver && rmdir $HAL/.S9Audio.new && ls -la $HAL/S9Audio.driver"
	ssh9 'mkdir -p /usr/local/bin && cat > /usr/local/bin/.s9play.new && chmod 755 /usr/local/bin/.s9play.new &&
		mv -f /usr/local/bin/.s9play.new /usr/local/bin/s9play' < jb/audio/s9play
	restart_mediaserverd
	;;
test)
	secs=${2:-4}
	out=logs/audio-$(date +%Y%m%d-%H%M%S).wav
	python3 -I tools/s9-audio-recv.py "$out" --seconds $((secs + 4)) &
	rp=$!
	sleep 1
	ssh9 "/usr/local/bin/s9play -t $secs"
	wait $rp
	;;
remove)
	ssh9 "rm -rf $HAL/.S9Audio.old $HAL/.S9Audio.new; [ ! -e $HAL/S9Audio.driver ] || mv $HAL/S9Audio.driver $HAL/.S9Audio.old"
	restart_mediaserverd
	;;
*)
	echo "usage: s9-audio.sh install | test [SECONDS] | remove" >&2
	exit 2
	;;
esac
