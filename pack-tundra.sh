#!/bin/bash
# Repack images for a test boot of the bring-up kernel.
# Takes the current boot.img and vendor_boot.img from the phone, swaps in
# the new Image (boot) and base DTB (vendor_boot), and builds dtbo.img from
# the tundra overlays. Ramdisks and cmdlines are kept as they are.
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

mkdir -p "$DEST"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

python3 "$MKB/unpack_bootimg.py" --boot_img "$IN/boot.img" \
	--out "$tmp/boot" --format mkbootimg > "$tmp/boot.args"
python3 "$MKB/unpack_bootimg.py" --boot_img "$IN/vendor_boot.img" \
	--out "$tmp/vendor_boot" --format mkbootimg > "$tmp/vendor_boot.args"

eval python3 "$MKB/mkbootimg.py" "$(cat "$tmp/boot.args")" \
	--kernel "$OUT/arch/arm64/boot/Image" --output "$DEST/boot.img"
eval python3 "$MKB/mkbootimg.py" "$(cat "$tmp/vendor_boot.args")" \
	--dtb "$DTS/lahaina-tundra-base.dtb" --vendor_boot "$DEST/vendor_boot.img"
python3 "$MKDTBO" create "$DEST/dtbo.img" --page_size=4096 \
	"$DTS"/lahaina-tundra-{evb,dvt1,dvt2}-overlay.dtbo

ls -l "$DEST"
