#!/usr/bin/env bash
# Pack a built tundra 6.18 kernel and dtbo into an AnyKernel3 flashable zip.
#
# The zip replaces the kernel in the active slot's boot partition, its dtbo,
# and the base DTB in its vendor_boot (see anykernel/anykernel.sh). The
# ramdisks and the kernel command line are left untouched.
#
# Usage:
#   DTBO=/path/dtbo.img ./flashable-zip-gen.sh # dtbo.img from pack-tundra.sh
#   KOUT=/path/out ./flashable-zip-gen.sh   # kernel output dir from build-tundra.sh
#   ZIP_NAME=foo.zip ./flashable-zip-gen.sh # override zip file name
#   ZIP_TAG=ksu ./flashable-zip-gen.sh      # torracat-tundra-6.1-ksu-<sha>-<date>.zip
#
# Flash: adb reboot sideload && adb sideload <zip>  (or install from recovery)
set -euo pipefail

SRC_DIR="$(cd "$(dirname "$0")" && pwd)"
TC="${TC:-$SRC_DIR/../toolchain}"
# Not $OUT: after `lunch` that is the ROM product dir.
KOUT="${KOUT:-$SRC_DIR/../out-full}"
DTBO="${DTBO:?set DTBO to the dtbo.img from pack-tundra.sh}"
IMAGE="$KOUT/arch/arm64/boot/Image"
DTS="$KOUT/arch/arm64/boot/dts/vendor/qcom"
# Pinned AnyKernel3, cached under $TC.
AK3_URL="${AK3_URL:-https://github.com/osm0sis/AnyKernel3}"
AK3_COMMIT="${AK3_COMMIT:-41f04bdb4ee30a44e429c2c1131d5e8bb32b15f1}"
AK3_DIR="$TC/AnyKernel3"

[ -f "$IMAGE" ] || { echo "ERROR: $IMAGE not found; run ./build-tundra.sh first" >&2; exit 1; }
[ -f "$DTBO" ] || { echo "ERROR: $DTBO not found" >&2; exit 1; }
[ -f "$DTS/lahaina-moto-base.dtb" ] || { echo "ERROR: base DTBs not found in $DTS" >&2; exit 1; }
command -v zip >/dev/null || { echo "ERROR: zip not installed" >&2; exit 1; }

mkdir -p "$TC"
if [ ! -d "$AK3_DIR/.git" ]; then
    git clone -q "$AK3_URL" "$AK3_DIR"
fi
if ! git -C "$AK3_DIR" cat-file -e "$AK3_COMMIT^{commit}" 2>/dev/null; then
    git -C "$AK3_DIR" fetch -q origin
fi

STAGE="$KOUT/anykernel"
rm -rf "$STAGE"
mkdir -p "$STAGE"
git -C "$AK3_DIR" archive "$AK3_COMMIT" | tar -x -C "$STAGE"
rm -rf "$STAGE/README.md" "$STAGE/modules" "$STAGE/patch" "$STAGE/ramdisk"
# Busybox applet fallback for recoveries that refuse symlinks in $AKHOME.
patch -s -p1 -d "$STAGE" --no-backup-if-mismatch < "$SRC_DIR/anykernel/setup_bb-fallback.patch"
cp "$SRC_DIR/anykernel/anykernel.sh" "$STAGE/anykernel.sh"
cp "$IMAGE" "$STAGE/Image"
cp "$DTBO" "$STAGE/dtbo.img"
cat "$DTS"/lahaina-moto-base-v2.1.dtb "$DTS"/lahaina-moto-base.dtb > "$STAGE/dtb"

KSHA=$(strings "$IMAGE" | sed -n '/Linux version/s/.*-g\([0-9a-f]\{12\}\).*/\1/p')
KSHA=${KSHA%%$'\n'*}
ZIP_NAME="${ZIP_NAME:-torracat-tundra-6.18${ZIP_TAG:+-$ZIP_TAG}-$KSHA-$(date -u +%Y%m%d-%H%M).zip}"
ZIP="$KOUT/$ZIP_NAME"
rm -f "$ZIP"
(cd "$STAGE" && zip -qr9 "$ZIP" . -x '.git*')

echo "[done] zip: $ZIP"
echo "       sha256: $(sha256sum "$ZIP" | cut -d' ' -f1)"
echo "       flash: adb reboot sideload && adb sideload $ZIP_NAME"
