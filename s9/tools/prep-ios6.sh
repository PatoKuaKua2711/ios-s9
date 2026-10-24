#!/bin/bash
# One-time root steps for boot-ios6 (internet over the USB cable + jailbreak).
#   sudo tools/prep-ios6.sh
# 1. This PC: a NetworkManager profile for the phone's USB Ethernet (host MAC
#    02:00:5e:77:ee:01, 192.168.7.1/24) and a dispatcher hook that shares the
#    PC's connection (NAT) while it is up. Every plug brings it up, unplugging
#    takes it down: nothing to run again.
# 2. Root image v2 = v1 (mkiosroot.sh + sweet-root-tweaks.py) + the Procursus
#    rootful bootstrap + jb/debs (installed by dpkg on the first boot) +
#    s9net + root's ssh key + the launchd jobs (tools/jb-launchd.py).
# Then (as the normal user, phone in TWRP): tools/flash-userdata.sh IMAGE.
set -euo pipefail
cd "$(dirname "$0")/.."
[ "$(id -u)" = 0 ] || { echo "run with sudo" >&2; exit 1; }
here=$(pwd)
src=../work/iosroot/ios16-root-v1.img
dst=../work/iosroot/ios16-root-v2.img
jb=$here/jb
fsck=$here/toolchain/bin/fsck.hfsplus

(cd "$jb" && sha256sum -c --quiet SHA256SUMS)

echo "== 1. PC: share the connection with the phone over USB"
nmcli connection delete s9-usb >/dev/null 2>&1 || true
nmcli connection add type ethernet con-name s9-usb ethernet.mac-address 02:00:5E:77:EE:01 \
	connection.autoconnect yes ipv4.method manual ipv4.addresses 192.168.7.1/24 \
	ipv4.never-default yes ipv6.method disabled >/dev/null
cat > /etc/NetworkManager/dispatcher.d/90-ios-s9-share <<'EOF'
#!/bin/sh
# Galaxy S9 iOS over USB (NetworkManager profile s9-usb): NAT this PC's
# connection to the phone (192.168.7.2) while its USB Ethernet is up.
[ "$CONNECTION_ID" = "s9-usb" ] || exit 0
IFACE=$1
rules() {
	iptables -t nat "$1" POSTROUTING -s 192.168.7.0/24 ! -o "$IFACE" -j MASQUERADE
	iptables "$1" FORWARD -i "$IFACE" -j ACCEPT
	iptables "$1" FORWARD -o "$IFACE" -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
}
case "$2" in
up)
	sysctl -qw net.ipv4.ip_forward=1
	rules -D 2>/dev/null
	iptables -t nat -A POSTROUTING -s 192.168.7.0/24 ! -o "$IFACE" -j MASQUERADE
	iptables -I FORWARD 1 -i "$IFACE" -j ACCEPT
	iptables -I FORWARD 1 -o "$IFACE" -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
	;;
down|pre-down)
	rules -D 2>/dev/null
	;;
esac
exit 0
EOF
chmod 755 /etc/NetworkManager/dispatcher.d/90-ios-s9-share
systemctl enable --now NetworkManager-dispatcher.service >/dev/null 2>&1 || true
echo "profile s9-usb + dispatcher hook installed"

echo "== 2. root image v2"
cp --reflink=auto --sparse=always "$src" "$dst"
loop=$(losetup -f --show "$dst")
mnt=$(mktemp -d)
cleanup() {
	mountpoint -q "$mnt" && umount "$mnt"
	losetup -d "$loop" 2>/dev/null || true
	rmdir "$mnt" 2>/dev/null || true
}
trap cleanup EXIT
"$fsck" -fy "$loop" >/dev/null || echo "fsck: exit $? (repaired or warnings)"
mount -t hfsplus -o rw,force "$loop" "$mnt"

# Procursus rootful bootstrap (CFVER 1800): /private/etc, /usr, /Library/dpkg ...
zstd -dc "$jb/bootstrap-rootful-1800.tar.zst" | tar -xp --numeric-owner --keep-directory-symlink -C "$mnt"
# ElleKit's files in place from the first boot (dpkg registers them later)
ar p "$jb/debs/0-ellekit.deb" data.tar.xz | tar -xJp --numeric-owner --keep-directory-symlink -C "$mnt"

install -d -m 755 "$mnt/private/var/ios-s9/debs"
install -m 644 "$jb"/debs/*.deb "$mnt/private/var/ios-s9/debs/"
install -m 755 "$jb/s9net" "$mnt/usr/libexec/s9net"
install -d -m 700 "$mnt/private/var/root/.ssh"
install -m 600 "$jb/authorized_keys" "$mnt/private/var/root/.ssh/authorized_keys"
chown -R 0:0 "$mnt/private/var/root/.ssh"

cat > "$mnt/usr/libexec/ios-s9-jbsetup" <<'EOF'
#!/bin/sh
# Galaxy S9 iOS: first-boot jailbreak setup (launchd job com.ios-s9.jbsetup, root).
exec >>/private/var/ios-s9/jbsetup.log 2>&1
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
[ -f /private/var/ios-s9/jbsetup.done ] && exit 0
echo "=== $(date) jbsetup start"
sleep 60        # let SpringBoard, lsd and installd come up for uicache
if [ -f /prep_bootstrap.sh ]; then
	NO_PASSWORD_PROMPT=1 /bin/sh /prep_bootstrap.sh
	printf 'alpine\n' | /usr/sbin/pw usermod 501 -h 0
	printf 'alpine\n' | /usr/sbin/pw usermod 0 -h 0
fi
# ElleKit injects via its loader (com.ios-s9.ellekit at boot); do NOT mark
# /.installed_taurine (that would make its postinst skip the loader).
dpkg -i /private/var/ios-s9/debs/*.deb || dpkg -i --force-depends /private/var/ios-s9/debs/*.deb
dpkg --configure -a
uicache -a
touch /private/var/ios-s9/jbsetup.done
echo "=== $(date) jbsetup done, respring"
sbreload || killall -9 SpringBoard
EOF
chmod 755 "$mnt/usr/libexec/ios-s9-jbsetup"

install -d -m 755 "$mnt/private/etc/apt/sources.list.d"
cat > "$mnt/private/etc/apt/sources.list.d/ios-s9.sources" <<'EOF'
Types: deb
URIs: https://repo.chariz.com/
Suites: ./
Components:

Types: deb
URIs: https://havoc.app/
Suites: ./
Components:

EOF

python3 -I "$here/tools/jb-launchd.py" "$mnt"
python3 -I "$here/tools/trim-daemons.py" "$mnt/System/Library/xpc/launchd.plist" --apply
sync
umount "$mnt"
"$fsck" -fy "$loop" >/dev/null || echo "fsck: exit $? (repaired or warnings)"
losetup -d "$loop"
trap - EXIT
rmdir "$mnt"
chown "${SUDO_UID:-0}:${SUDO_GID:-0}" "$dst"
echo "image: $dst"
echo "next (normal user, phone in TWRP): tools/flash-userdata.sh $dst"
