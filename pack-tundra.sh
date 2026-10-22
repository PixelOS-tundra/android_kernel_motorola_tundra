#!/bin/bash
# Repack images for a test boot of the bring-up kernel.
# Takes the current boot.img and vendor_boot.img from the phone, swaps in
# the new Image (boot) and base DTB (vendor_boot), and builds dtbo.img from
# the tundra overlays. Ramdisks and cmdlines are kept as they are, except
# that by default (SAFE_FSTAB=1) "formattable" and "check" are stripped from
# the first-stage fstab, so a misbehaving storage driver can never make
# fs_mgr fsck or reformat /metadata or /data.
# DTB=<file> overrides the DTB put into vendor_boot; DTB=keep keeps the one
# already in the input vendor_boot.img.
# Usage: ./pack-tundra.sh <dir with boot.img + vendor_boot.img> [dest-dir]
set -e
SRC=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$SRC/../out-6.1}
TOOLS=${TOOLS:-$HOME/k61/toolchain}
MKB=$TOOLS/mkbootimg
MKDTBO=$TOOLS/libufdt/utils/src/mkdtboimg.py
IN=$(cd "$1" && pwd)
DEST=${2:-$SRC/../images/test}
DTS=$OUT/arch/arm64/boot/dts/vendor/qcom
SAFE_FSTAB=${SAFE_FSTAB:-1}
DTB=${DTB:-$DTS/lahaina-tundra-base.dtb}

mkdir -p "$DEST"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

python3 "$MKB/unpack_bootimg.py" --boot_img "$IN/boot.img" \
	--out "$tmp/boot" --format mkbootimg > "$tmp/boot.args"
python3 "$MKB/unpack_bootimg.py" --boot_img "$IN/vendor_boot.img" \
	--out "$tmp/vendor_boot" --format mkbootimg > "$tmp/vendor_boot.args"

vendor_ramdisk=$tmp/vendor_boot/vendor_ramdisk
if [ "$SAFE_FSTAB" = 1 ]; then
	mkdir "$tmp/vr"
	lz4 -dc "$vendor_ramdisk" | (cd "$tmp/vr" && cpio -idm --quiet)
	sed -i -E '/^\S+\s+\/(metadata|data)\s/{s/(,|\s)formattable(,)?/\1/;s/(,|\s)check(,)?/\1/;s/,(\s|$)/\1/;s/\s,/ /}' \
		"$tmp/vr/first_stage_ramdisk/fstab.qcom"
	grep -E "^\S+\s+/(metadata|data)\s" "$tmp/vr/first_stage_ramdisk/fstab.qcom"
	(cd "$tmp/vr" && find . -mindepth 1 -printf "%P\n" | LC_ALL=C sort | cpio -o -H newc -R 0:0 --quiet) |
		lz4 -l -12 --favor-decSpeed > "$tmp/vendor_ramdisk.safe"
	vendor_ramdisk=$tmp/vendor_ramdisk.safe
fi

eval python3 "$MKB/mkbootimg.py" "$(cat "$tmp/boot.args")" \
	--kernel "$OUT/arch/arm64/boot/Image" --output "$DEST/boot.img"
[ "$DTB" = keep ] && DTB=$tmp/vendor_boot/dtb
eval python3 "$MKB/mkbootimg.py" "$(cat "$tmp/vendor_boot.args")" \
	--dtb "$DTB" --vendor_ramdisk "$vendor_ramdisk" \
	--vendor_boot "$DEST/vendor_boot.img"
python3 "$MKDTBO" create "$DEST/dtbo.img" --page_size=4096 \
	"$DTS"/lahaina-tundra-{evb,dvt1,dvt2}-overlay.dtbo

ls -l "$DEST"
