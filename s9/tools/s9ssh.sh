#!/bin/sh
# Shell (or one command) on the phone's iOS over the USB cable: root@192.168.7.2
# with the PC's key ~/.ssh/ios-s9 (installed by tools/prep-ios6.sh).
#   tools/s9ssh.sh [COMMAND ...]
exec ssh -i "$HOME/.ssh/ios-s9" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
	-o LogLevel=ERROR -o ConnectTimeout=30 root@192.168.7.2 "$@"
