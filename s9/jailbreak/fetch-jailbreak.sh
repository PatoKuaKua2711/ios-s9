#!/bin/bash
# Download the third-party jailbreak pieces for the Galaxy S9 iOS root image.
#
# This repository does NOT redistribute these binaries -- they belong to their
# authors under their own licenses (see CREDITS). This script fetches them from
# their official sources into ./jb/, which tools/prep-ios6.sh then installs into
# the iOS root. Architecture: iphoneos-arm (rootful, iOS 16 / CFVER 1800).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p jb/debs

echo "== palera1n rootful bootstrap (Procursus, CFVER 1800) =="
curl -fsSL -o jb/bootstrap-rootful-1800.tar.zst https://static.palera.in/bootstrap-1800.tar.zst

echo "== core packages =="
dl() { echo "  $1"; curl -fsSL -o "jb/debs/$2" "$1"; }
dl https://ellekit.space/pool/ellekit_1.2_iphoneos-arm.deb                                   0-ellekit.deb
dl https://github.com/Sileo/Sileo/releases/download/2.5.1/org.coolstar.sileo_2.5.1_iphoneos-arm.deb 1-sileo.deb
dl https://repo.chariz.com/debs/ws.hbang.newterm3_3.0~beta1_iphoneos-arm.deb                 2-newterm.deb
dl https://apt.procurs.us/pool/main/iphoneos-arm64/1700/libpcap/libpcapa_1.10.4_iphoneos-arm.deb 3-libpcapa.deb
dl https://apt.procurs.us/pool/main/iphoneos-arm64/1700/network-cmds/network-cmds_669_iphoneos-arm.deb 4-network-cmds.deb
for p in openssh-client openssh-server openssh-sftp-server; do
  dl "https://apt.procurs.us/pool/main/iphoneos-arm64/1700/openssh/${p}_9.7p1-1_iphoneos-arm.deb" "5-${p}.deb"
done

echo "== your SSH key (for root@192.168.7.2 over the USB cable) =="
[ -f jb/authorized_keys ] || cp "${HOME}/.ssh/ios-s9.pub" jb/authorized_keys 2>/dev/null || \
  echo "  (put your public key at jb/authorized_keys)"

echo "done. PreferenceLoader and other tweak deps are installed on-device via apt"
echo "(BigBoss/Procursus repos), so tweaks resolve their dependencies by themselves."
