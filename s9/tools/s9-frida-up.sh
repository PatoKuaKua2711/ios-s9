#!/bin/sh
# Start frida-server on the phone's iOS listening on the USB network (the
# .deb's launchd job only listens on localhost and stays loaded at boot:
# it is unloaded here, the server runs only while we need it).
# Careful: attaching to SpringBoard hung it more than once (stuck inside dyld,
# Apple logo), and LSApplicationWorkspace registerPlugin: from an injected
# process wedged lsd (no app would open). Use it on disposable processes;
# change SpringBoard with a defensive tweak instead (s9/ios-tools).
cd "$(dirname "$0")/.."
tools/s9ssh.sh 'launchctl unload /Library/LaunchDaemons/re.frida.server.plist 2>/dev/null;
ps -axo comm | grep -q frida-server || (nohup /usr/sbin/frida-server -l 0.0.0.0 >/dev/null 2>&1 &); sleep 1; ps -axo pid,comm | grep frida-server'
