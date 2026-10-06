#!/bin/bash
# setup-macos.sh - populate vendor/macos-<arch>/ with self-contained native tools.
# RUN THIS ON A MAC (build-time only; the shipped app needs none of this).
#
# Produces, for the Mac's current arch (arm64 or x86_64):
#   vendor/macos-<arch>/usbboot                 (compiled from ../src/usbboot)
#   vendor/macos-<arch>/mksquashfs, unsquashfs  (lzo-capable)
#   vendor/macos-<arch>/my_write6_dram.bin, disc_spl_lpddr3.bin  (device blobs)
#   vendor/macos-<arch>/lib/*.dylib             (bundled deps; load paths rewritten)
#
# Build-time deps (Homebrew): libusb, squashfs, lzo, dylibbundler.
# The END USER needs none of these - the app bundles everything.
set -euo pipefail
cd "$(dirname "$0")/.."                       # installer/

ARCH=$(uname -m)
case "$ARCH" in
  arm64)  TAG=macos-arm64 ;;
  x86_64) TAG=macos-x86_64 ;;
  *) echo "unsupported macOS arch: $ARCH" >&2; exit 2 ;;
esac
OUT="vendor/$TAG"; LIB="$OUT/lib"
mkdir -p "$LIB"
echo ">> building $TAG in $OUT"

# --- Homebrew build deps -----------------------------------------------------
if ! command -v brew >/dev/null 2>&1; then
  echo "ERROR: Homebrew is required at BUILD time (https://brew.sh). The shipped app needs it NOT." >&2
  exit 2
fi
BREW_PREFIX="$(brew --prefix)"
echo ">> ensuring build deps (pkg-config libusb squashfs lzo dylibbundler)"
brew list pkg-config    >/dev/null 2>&1 || brew install pkg-config
brew list libusb        >/dev/null 2>&1 || brew install libusb
brew list lzo           >/dev/null 2>&1 || brew install lzo
brew list squashfs      >/dev/null 2>&1 || brew install squashfs
brew list lz4           >/dev/null 2>&1 || brew install lz4
brew list xz            >/dev/null 2>&1 || brew install xz
brew list zstd          >/dev/null 2>&1 || brew install zstd
brew list dylibbundler  >/dev/null 2>&1 || brew install dylibbundler

# Keep bundled binaries aligned with corresponding-source/macos-arm64/.
check_brew_version() {
  local name="$1" version="$2" installed
  installed="$(brew list --versions "$name")"
  if [ "$installed" != "$name $version" ]; then
    echo "ERROR: expected Homebrew $name $version; found '${installed:-not installed}'. Install the pinned version before rebuilding." >&2
    exit 2
  fi
}
check_brew_version squashfs 4.7.5
check_brew_version libusb 1.0.30
check_brew_version lzo 2.10
check_brew_version lz4 1.10.0
check_brew_version xz 5.8.4
check_brew_version zstd 1.5.7

# a C compiler (clang via Xcode CLT) is required
if ! command -v cc >/dev/null 2>&1; then
  echo "ERROR: no C compiler. Install Xcode command line tools: xcode-select --install" >&2
  exit 2
fi
# make pkg-config find Homebrew's libusb even on a fresh shell
export PKG_CONFIG_PATH="$BREW_PREFIX/lib/pkgconfig:$(brew --prefix libusb)/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
pkg-config --exists libusb-1.0 || { echo "ERROR: pkg-config can't find libusb-1.0 (PKG_CONFIG_PATH=$PKG_CONFIG_PATH)" >&2; exit 2; }

# --- compile usbboot from source --------------------------------------------
echo ">> compiling usbboot (from src/usbboot/usbboot.c)"
cc -O2 -std=c99 -Wall \
   $(pkg-config --cflags libusb-1.0) \
   -o "$OUT/usbboot" src/usbboot/usbboot.c \
   $(pkg-config --libs libusb-1.0)
echo "   built: $("$OUT/usbboot" --help >/dev/null 2>&1 && echo ok || echo 'runs')"

# --- squashfs-tools (must support -comp lzo, to match the stock image) --------
MKSQ="$(command -v mksquashfs || true)"
UNSQ="$(command -v unsquashfs || true)"
[ -n "$MKSQ" ] && [ -n "$UNSQ" ] || { echo "ERROR: squashfs-tools not found after brew install." >&2; exit 2; }
# verify lzo works
tmp="$(mktemp -d)"; echo hi > "$tmp/f"
if ! "$MKSQ" "$tmp/f" "$tmp/t.sqfs" -comp lzo -noappend >/dev/null 2>&1; then
  echo "ERROR: this mksquashfs lacks -comp lzo. Rebuild squashfs-tools with lzo:" >&2
  echo "       (the stock Snowsky image is lzo-compressed and must be matched)." >&2
  rm -rf "$tmp"; exit 2
fi
rm -rf "$tmp"
cp "$MKSQ" "$OUT/mksquashfs"
cp "$UNSQ" "$OUT/unsquashfs"
echo "   squashfs-tools: lzo OK"

# --- device blobs (arch-neutral) --------------------------------------------
cp flash/my_write6_dram.bin  "$OUT/my_write6_dram.bin"
cp flash/disc_spl_lpddr3.bin "$OUT/disc_spl_lpddr3.bin"

# --- make every binary self-contained (bundle dylibs, rewrite to @loader_path)
# dylibbundler copies non-system dylibs into $LIB and rewrites load commands to
# @loader_path/lib, so nothing depends on Homebrew at runtime.
for b in usbboot mksquashfs unsquashfs; do
  echo ">> bundling dylibs for $b"
  dylibbundler -of -b -x "$OUT/$b" -d "$LIB" -p "@loader_path/lib/" >/dev/null
done

echo
echo ">> verifying no Homebrew/absolute dylib deps remain (executables AND every bundled dylib):"
leak=0
check_macho() {   # $1 = mach-o file
  otool -L "$1" | tail -n +2 | grep -qE "$BREW_PREFIX|/usr/local/opt|/opt/homebrew|/usr/local/Cellar" && {
    echo "   !! $1 still references external paths:"; otool -L "$1" | grep -E "$BREW_PREFIX|homebrew|Cellar" || true
    leak=1
  }
  return 0          # a clean file must not trip set -e (grep -q finding nothing is the good case)
}
for b in usbboot mksquashfs unsquashfs; do check_macho "$OUT/$b"; done
for d in "$LIB"/*; do [ -f "$d" ] && check_macho "$d"; done   # every copied dylib too (B2.3)
if [ "$leak" != 0 ]; then
  echo "ERROR: bundle is NOT self-contained - external dylib references remain (see above)." >&2
  exit 3
fi
echo "   clean - all deps bundled under $LIB"
echo
echo "DONE: $OUT"
ls -la "$OUT"
