#!/bin/bash
# build-squashfs-static.sh - build mksquashfs + unsquashfs for Linux,
# with the compressors the installer actually uses.
#
# The installer repacks the rootfs with `-comp lzo -b 131072` and unpacks the
# stock (lzo) rootfs, so LZO is mandatory; gzip + xz are included for reading
# other firmware. Static builds have no liblzo2/libz/liblzma/glibc-version
# runtime deps on the user's machine.
#
# Output: installer/vendor/linux-x86_64/{mksquashfs,unsquashfs} (stripped).
# A dynamic fallback is for this host only; stage-release.sh refuses it.
# A cosmetic getpwuid NSS warning at link time is expected (owner-name display in
# the summary only; packing uses numeric uid/gid) and does not affect output.
set -euo pipefail
cd "$(dirname "$0")/.."                       # installer/
OUTDIR="$(pwd)/vendor/linux-x86_64"

verify_sha256() {
  if command -v sha256sum >/dev/null 2>&1; then
    printf '%s  %s\n' "$1" "$2" | sha256sum -c -
  elif command -v shasum >/dev/null 2>&1; then
    printf '%s  %s\n' "$1" "$2" | shasum -a 256 -c -
  else
    echo "ERROR: sha256sum or shasum is required to verify source" >&2
    return 1
  fi
}

WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
echo "==> fetching squashfs-tools source"
if ! command -v apt-get >/dev/null 2>&1 || ! apt-get source squashfs-tools >/dev/null 2>&1; then
  SQUASHFS_VER="4.6.1"
  SQUASHFS_URL="https://github.com/plougher/squashfs-tools/archive/refs/tags/${SQUASHFS_VER}.tar.gz"
  SQUASHFS_TAR="$WORK/squashfs-tools-${SQUASHFS_VER}.tar.gz"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$SQUASHFS_URL" -o "$SQUASHFS_TAR"
  elif command -v wget >/dev/null 2>&1; then
    wget -qO "$SQUASHFS_TAR" "$SQUASHFS_URL"
  else
    echo "ERROR: failed to fetch squashfs-tools source (apt-get source, curl, or wget required)" >&2
    exit 3
  fi
  verify_sha256 9c4974e07c61547dae14af4ed1f358b7d04618ae194e54d6be72ee126f0d2f53 "$SQUASHFS_TAR" || exit 3
  tar -xzf "$SQUASHFS_TAR"
fi
cd squashfs-tools-*/squashfs-tools

echo "==> building (gzip+lzo+xz)"
make clean >/dev/null 2>&1 || true
STATIC_MODE=1
if ! make GZIP_SUPPORT=1 LZO_SUPPORT=1 XZ_SUPPORT=1 \
     EXTRA_CFLAGS="-static" LDFLAGS="-static" -j"$(nproc)" mksquashfs unsquashfs >/dev/null 2>&1; then
  echo "==> static link failed (static libs missing on this distro), building dynamically for host..."
  make clean >/dev/null 2>&1 || true
  make GZIP_SUPPORT=1 LZO_SUPPORT=1 XZ_SUPPORT=1 -j"$(nproc)" mksquashfs unsquashfs >/dev/null
  STATIC_MODE=0
fi
strip mksquashfs unsquashfs

if [ "$STATIC_MODE" = "1" ]; then
  for b in mksquashfs unsquashfs; do
    file "$b" | grep -q "statically linked" || { echo "ERROR: $b not static" >&2; exit 5; }
  done
else
  echo "NOTE: built dynamically linked binaries for local host use in $OUTDIR."
  echo "      stage-release.sh will refuse them; build static tools before staging a release."
fi

# prove the installer's exact LZO path round-trips before installing
t="$WORK/smoke"; mkdir -p "$t/src"; echo diskos > "$t/src/f"
./mksquashfs "$t/src" "$t/o.sqsh" -comp lzo -b 131072 -noappend >/dev/null 2>&1
./unsquashfs -d "$t/u" "$t/o.sqsh" >/dev/null 2>&1
diff -r "$t/src" "$t/u" >/dev/null || { echo "ERROR: LZO round-trip failed" >&2; exit 6; }
echo "==> LZO round-trip OK"

for b in mksquashfs unsquashfs; do
  install -m 0755 "$b" "$OUTDIR/$b"
  echo "==> installed $OUTDIR/$b  ($(sha256sum "$OUTDIR/$b" | cut -d' ' -f1))"
done
