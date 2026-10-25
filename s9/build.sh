#!/bin/bash
# Build the S9 shim and wrap it the way Samsung's ABL expects the stock
# kernel: boot.img header v0, Image.gz with the stock DTBs appended.
#   ./build.sh [m1|m2|m3|m5|m6] stage to build (default m2)
#   PAYLOAD=out/payload.bin ./build.sh m4   XNU payload as the ramdisk
#   out/boot.img      -> tools/flash-twrp.sh (verified dd), also kept as out/boot-STAGE.img
#   out/boot.tar.md5  -> Odin, AP slot
# Based on ios-sweet shim/build.sh (MIT, dipa-studio).
set -euo pipefail
cd "$(dirname "$0")"
# Arch's cross gcc unpacked into toolchain/root (no root needed), unless told otherwise.
if [ -z "${CROSS_COMPILE:-}" ] && [ -x toolchain/root/usr/bin/aarch64-linux-gnu-gcc ]; then
	CROSS_COMPILE=toolchain/root/usr/bin/aarch64-linux-gnu-
fi
CROSS=${CROSS_COMPILE:-aarch64-linux-gnu-}
CC=${CROSS}gcc
OBJCOPY=${CROSS}objcopy
READELF=${CROSS}readelf
NM=${CROSS}nm
STOCK_BOOT=${STOCK_BOOT:-../ZTO-G9600ZHU9FVF2/boot.img}
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -fno-tree-loop-distribute-patterns -Wall -Wextra"

STAGE=${1:-m2}
case $STAGE in
m1) main="m1.c screen.c" ;;
m2) main="m2.c con.c" ;;
m3) main="m3.c con.c plat.c ufs.c mmu.c" ;;
m5) main="m5.c con.c plat.c" ;;
m6) main="m6.c con.c plat.c mmu.c" ;;
m7) main="m7.c con.c plat.c" ;;
m8) main="m8.c con.c plat.c" ;;
m9) main="m9.c con.c plat.c" ;;
m10) main="m10.c con.c plat.c" ;;
m11) main="m11.c con.c plat.c" ;;
m4) main="m4.c con.c plat.c loader.c"
    [ -f "${PAYLOAD:-}" ] || { echo "m4 needs PAYLOAD=path/to/payload.bin (tools/mkpayload.py)" >&2; exit 2; } ;;
*) echo "usage: build.sh [m1|m2|m3|m4|m5|m6|m7|m8|m9|m10|m11]" >&2; exit 2 ;;
esac

mkdir -p out/obj
rm -f out/obj/*.o
objs=()
for f in start.S $main fb.c mdss.c fdt.c ramlog.c lib.c; do
	o=out/obj/${f%.*}.o
	"$CC" $CFLAGS -c "shim/$f" -o "$o"
	objs+=("$o")
done
# ABL runs us wherever it loads us, with no relocation pass: only the
# header's image_size (start.o, linked at 0) may be an absolute address.
abs=$("$READELF" -r "${objs[@]:1}" | grep -c ABS64 || true)
[ "$abs" = 0 ] || { echo "error: $abs absolute relocations in C objects" >&2; exit 1; }
"$CC" $CFLAGS -static -T shim/shim.ld -Wl,--build-id=none -Wl,--no-warn-rwx-segments -Wl,-N \
	"${objs[@]}" -o out/shim.elf
"$OBJCOPY" -O binary out/shim.elf out/Image
# ABL copies the file; pad to image_size so .bss/stack exist as zeroed bytes.
size=$("$NM" out/shim.elf | awk '$3=="_end"{print strtonum("0x"$1)}')
truncate -s "$size" out/Image
gzip -9 -n -c out/Image > out/Image.gz

# Same DTB list as the stock kernel: ABL still picks one by msm-id/board-id.
[ -f dtb/stock-dtbs.bin ] || python3 -I tools/extract-stock-dtbs.py "$STOCK_BOOT" dtb/stock-dtbs.bin
cat out/Image.gz dtb/stock-dtbs.bin > out/Image.gz-dtb

# Ramdisk: the XNU payload for m4; otherwise an empty newc cpio (some ABL builds
# dislike a zero-size ramdisk), fixed mtime and inodes so hashes are reproducible.
if [ "$STAGE" = m4 ]; then
	cp "$PAYLOAD" out/ramdisk.cpio.gz
else
	(cd out && rm -rf rd && mkdir rd && cd rd && touch -d @0 . && find . | cpio -o -H newc --reproducible --quiet | gzip -9 -n > ../ramdisk.cpio.gz)
fi

# Header values from the stock G9600ZHU9FVF2 boot.img (unpack_bootimg).
mkbootimg --header_version 0 --kernel out/Image.gz-dtb --ramdisk out/ramdisk.cpio.gz \
	--base 0x00000000 --kernel_offset 0x00008000 --ramdisk_offset 0x02000000 \
	--second_offset 0x00f00000 --tags_offset 0x01e00000 --pagesize 4096 \
	--board RILQI20A009 --os_version 10.0.0 --os_patch_level 2022-05 \
	--cmdline "console=null" -o out/boot.img

# Odin: tar with the image, followed by the tar's own md5 line.
(cd out && tar -H ustar -cf boot.tar boot.img && md5sum -t boot.tar >> boot.tar && mv boot.tar boot.tar.md5)

# Host render of the M1 screen layout (fake probe values) for a quick look.
if [ "$STAGE" = m1 ]; then
	gcc -O1 -Wall -o out/host_test shim/host_test.c shim/fb.c shim/screen.c
	./out/host_test out/m1-preview.ppm && python3 -I tools/ppm2png.py out/m1-preview.ppm out/m1-preview.png 2
	rm -f out/m1-preview.ppm
fi
cp out/boot.img "out/boot-$STAGE.img"

ls -l out/Image out/boot.img out/boot.tar.md5
sha256sum out/boot.img
