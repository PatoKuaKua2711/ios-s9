#!/bin/sh
# iOS -> TWRP without the buttons: the next boot reason goes into the PMIC
# (kernel sysctl kern.sweet_next_boot, patch 0090), then a normal reboot.
set -e
"$(dirname "$0")/s9ssh.sh" 'sysctl -w kern.sweet_next_boot=1 && sync && reboot'
adb wait-for-recovery
echo "phone in TWRP"
