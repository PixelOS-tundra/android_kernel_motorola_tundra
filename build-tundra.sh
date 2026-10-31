#!/usr/bin/env bash
# Standalone build for the tundra 5.4 kernel (branch: seventeen / seventeen-ksu).
#
# Sets up an AOSP Clang toolchain under $TC (default: $HOME/tc) and builds
# Image.gz + dtbs with LLVM. Run on a Linux build host (the Mac checkout cannot
# build a kernel: case-insensitive FS, no Linux/ELF toolchain).
#
# The Clang version matches this tree's CONFIG_CC_VERSION_TEXT (AOSP clang
# based on r596125, clang 22.0.2) so warnings-as-errors behave as on the server.
#
# Usage:
#   ./build-tundra.sh                 # build with defaults
#   JOBS=48 ./build-tundra.sh         # override parallelism
#   TC=/data/tc ./build-tundra.sh     # override toolchain dir
#   CLANG_SRC=/path/to/clang-r596125 ./build-tundra.sh   # copy clang from here
#   CLANG_URL=https://.../clang-r596125.tar.gz ./build-tundra.sh  # or download
set -euo pipefail

# --- config (all overridable from the environment) ---
TC="${TC:-$HOME/tc}"
CLANG_VERSION="${CLANG_VERSION:-r596125}"            # matches CONFIG_CC_VERSION_TEXT
CLANG_DIR="$TC/clang-$CLANG_VERSION"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 8)}"
OUT="${OUT:-$PWD/out}"
DEFCONFIG="${DEFCONFIG:-tundra_defconfig}"
# An already-present AOSP clang to copy from if $CLANG_DIR is empty.
# The PixelOS build server keeps exactly this version here:
CLANG_SRC="${CLANG_SRC:-$HOME/pos/prebuilts/clang/host/linux-x86/clang-$CLANG_VERSION}"
# Optional direct tarball URL (a clang-$CLANG_VERSION.tar.gz) if no local copy.
CLANG_URL="${CLANG_URL:-}"

KROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$KROOT"

# --- toolchain setup ($TC) ---
setup_clang() {
    if [ -x "$CLANG_DIR/bin/clang" ]; then
        echo "[tc] clang $CLANG_VERSION already at $CLANG_DIR"
        return
    fi
    mkdir -p "$TC"
    if [ -x "$CLANG_SRC/bin/clang" ]; then
        echo "[tc] copying clang $CLANG_VERSION from $CLANG_SRC"
        cp -a "$CLANG_SRC" "$CLANG_DIR"
    elif [ -n "$CLANG_URL" ]; then
        echo "[tc] downloading clang $CLANG_VERSION from $CLANG_URL"
        mkdir -p "$CLANG_DIR"
        curl -L --fail "$CLANG_URL" | tar -xz -C "$CLANG_DIR"
    else
        cat >&2 <<EOF
ERROR: no clang at $CLANG_DIR, no local copy at
       $CLANG_SRC, and CLANG_URL is unset.
Provide AOSP clang $CLANG_VERSION one of these ways, then re-run:
  - set CLANG_SRC to an existing clang-$CLANG_VERSION dir, or
  - set CLANG_URL to a clang-$CLANG_VERSION.tar.gz to download.
(The gitiles +archive endpoint cannot serve the clang tree: it exceeds the
archive size limit, so there is no single canonical download URL.)
EOF
        exit 1
    fi
    [ -x "$CLANG_DIR/bin/clang" ] || { echo "ERROR: clang still missing after setup" >&2; exit 1; }
}

setup_clang
export PATH="$CLANG_DIR/bin:$PATH"

MAKE_ARGS=(
    O="$OUT"
    ARCH=arm64
    LLVM=1
    LLVM_IAS=1
    CROSS_COMPILE=aarch64-linux-gnu-
    CC=clang
)

echo "[build] clang:     $(clang --version | head -1)"
echo "[build] defconfig: $DEFCONFIG"
echo "[build] jobs:      $JOBS"
echo "[build] out:       $OUT"

make "${MAKE_ARGS[@]}" "$DEFCONFIG"
make "${MAKE_ARGS[@]}" -j"$JOBS" Image.gz dtbs

echo
echo "[done] kernel: $OUT/arch/arm64/boot/Image.gz"
echo "[done] dtbs:   $OUT/arch/arm64/boot/dts/"
echo "[done] repack into a boot image to flash/test (Image.gz + appended dtb)."
