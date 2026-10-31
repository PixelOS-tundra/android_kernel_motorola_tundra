#!/usr/bin/env bash
# Pack a built tundra 5.4 kernel into an AnyKernel3 flashable zip.
#
# Run after ./build-tundra.sh. The zip replaces only the kernel in the active
# slot's boot partition (split_boot + flash_boot, see anykernel/anykernel.sh);
# the ramdisk and the vendor_boot DTB (boot header v3) are left untouched.
#
# Usage:
#   ./flashable-zip-gen.sh                  # uses ./out/arch/arm64/boot/Image
#   KOUT=/path/out ./flashable-zip-gen.sh   # kernel output dir from build-tundra.sh
#   ZIP_NAME=foo.zip ./flashable-zip-gen.sh # override zip file name
#
# Flash: adb reboot sideload && adb sideload <zip>  (or install from recovery)
set -euo pipefail

SRC_DIR="$(cd "$(dirname "$0")" && pwd)"
TC="${TC:-$HOME/tc}"
# Not $OUT: after `lunch` that is the ROM product dir.
KOUT="${KOUT:-$SRC_DIR/out}"
IMAGE="$KOUT/arch/arm64/boot/Image"
# Pinned AnyKernel3, cached under $TC.
AK3_URL="${AK3_URL:-https://github.com/osm0sis/AnyKernel3}"
AK3_COMMIT="${AK3_COMMIT:-41f04bdb4ee30a44e429c2c1131d5e8bb32b15f1}"
AK3_DIR="$TC/AnyKernel3"

[ -f "$IMAGE" ] || { echo "ERROR: $IMAGE not found; run ./build-tundra.sh first" >&2; exit 1; }
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
cp "$SRC_DIR/anykernel/anykernel.sh" "$STAGE/anykernel.sh"
cp "$IMAGE" "$STAGE/Image"

ZIP_NAME="${ZIP_NAME:-torracat-tundra-5.4-$(git -C "$SRC_DIR" rev-parse --short=12 HEAD)-$(date -u +%Y%m%d-%H%M).zip}"
ZIP="$KOUT/$ZIP_NAME"
rm -f "$ZIP"
(cd "$STAGE" && zip -qr9 "$ZIP" . -x '.git*')

echo "[done] zip: $ZIP"
echo "       sha256: $(sha256sum "$ZIP" | cut -d' ' -f1)"
echo "       flash: adb reboot sideload && adb sideload $ZIP_NAME"
