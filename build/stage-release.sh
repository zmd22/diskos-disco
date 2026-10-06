#!/bin/bash
# stage-release.sh - assemble the release tarball(s) into build/release/ and generate SHA256SUMS.
#
# The release ships as SOURCE (run with your own Python via ./install.sh) plus the native flash
# tools for the target platform - NOT a bundled onefile binary. Each tarball is self-contained:
# extract it, run ./install.sh once, then ./diskos-installer.
#
# Usage:
#   build/stage-release.sh [TAG ...]      # default TAG: the host's (e.g. linux-x86_64)
# Needs a git checkout with the release committed (git archive of DISKOS_REF, default HEAD).
# Build the native tools for each TAG first (Linux: build/build-*-static.sh; macOS: vendor/setup-macos.sh).
set -euo pipefail
cd "$(dirname "$0")/.."                        # installer/
ROOT=$(pwd)
REL="$ROOT/build/release"
mkdir -p "$REL"

# default to the host platform tag if none given
if [ "$#" -eq 0 ]; then
  _os=$(uname -s | tr '[:upper:]' '[:lower:]'); case "$_os" in darwin) _os=macos;; esac
  _arch=$(uname -m); case "$_arch" in amd64) _arch=x86_64;; aarch64|arm64) _arch=arm64;; esac
  set -- "${_os}-${_arch}"
fi

# The tarball is built from `git archive` of REF (default HEAD), never from the working tree, so untracked
# local files (scratch tools, unused assets, signing keys) cannot leak in. Commit what belongs in the release
# first; set DISKOS_REF=<tag|commit> to stage something other than HEAD.
REF=${DISKOS_REF:-HEAD}
git -C "$ROOT" rev-parse --verify --quiet "$REF^{commit}" >/dev/null || { echo "bad ref: $REF" >&2; exit 1; }
[ "$(git -C "$ROOT" rev-parse --show-toplevel)" = "$ROOT" ] || { echo "installer/ must be the git root" >&2; exit 1; }

# Paths that must NEVER appear in a tarball, even if someone commits them by mistake (checked after extract).
FORBIDDEN=(signing '*.key' '*.squashfs' '*.sqfs' 'diskos_dev*.bin' 'diskos_public*.bin' '*_recovery.bin'
  payload/diskos-prefstat ui/tools/diskos_prefstat.c _retired_unlicensed flash/qual_lpddr3.sh flash/scan_check.sh
  flash/my_write5_scan_dram.bin)
# Unused local screenshots: only these docs/assets images may ever ship if untracked ones appear.
BLOCK_ASSETS=(audiobook-np.png books.png chapters.png quicksettings.png)

# Host-side tools taken from vendor/<TAG>/ (untracked/ignored build outputs): an EXPLICIT allowlist only.
REQ_VENDOR=(usbboot mksquashfs unsquashfs my_write6_dram.bin disc_spl_lpddr3.bin)
# Payload that MUST exist in the archive (committed at REF); nothing else from the working tree is added.
REQ_PAYLOAD=(mq_ui S96diskos_select S97diskos_install S99usbserial diskos-debug.sh dropbearmulti diskos-rmguard diskos-selected diskos-bootprobe diskos-artdec diskos-launch diskos-verify.sh diskos-root.pub.pem)
MACOS_DYLIBS=(libusb-1.0.0.dylib liblzo2.2.dylib liblz4.1.10.0.dylib liblzma.5.dylib libzstd.1.5.7.dylib)
# Other files that MUST be in the archive (writer + its build inputs, launcher source, verifier support).
REQ_ARCHIVE=(flash/my_write6_dram.bin flash/my_write6.c flash/build_nand.sh flash/dram_head.bin flash/dram_head.S flash/dram_head.ld flash/dram.ld
  flash/sha256_min.h flash/disc_spl_lpddr3.bin src/launcher/diskos_launch.c build/build-launcher.sh
  diskos_installer/basegate.py ui/tools/diskos_artdec.c ui/tools/stb_image.h vendor/setup-macos.sh)

made=()
for TAG in "$@"; do
  echo "== staging $TAG (from $REF) =="
  miss=()
  for f in "${REQ_VENDOR[@]}"; do [ -f "vendor/$TAG/$f" ] || miss+=("vendor/$TAG/$f"); done
  if [ "${#miss[@]}" -gt 0 ]; then
    echo "  FAIL $TAG - missing: ${miss[*]}" >&2
    echo "  (build the native tools for $TAG first; every requested platform must stage, or nothing is published)" >&2
    exit 1
  fi

  if [[ "$TAG" == linux-* ]]; then
    if [ -e "vendor/$TAG/lib" ] || [ -L "vendor/$TAG/lib" ]; then
      echo "  FAIL $TAG - static Linux tools must not have vendor/$TAG/lib" >&2
      exit 1
    fi
    command -v file >/dev/null 2>&1 || { echo "  FAIL $TAG - file is required to check native tools" >&2; exit 1; }
    for f in usbboot mksquashfs unsquashfs; do
      path="vendor/$TAG/$f"
      kind=$(file -b "$path")
      if [ -L "$path" ] || [[ "$kind" != *"statically linked"* ]]; then
        echo "  FAIL $TAG - $path is not a regular statically linked binary: $kind" >&2
        exit 1
      fi
    done
  fi
  if [[ "$TAG" == macos-* ]]; then
    if [ ! -d "vendor/$TAG/lib" ] || [ -L "vendor/$TAG/lib" ]; then
      echo "  FAIL $TAG - vendor/$TAG/lib must be a directory" >&2
      exit 1
    fi
  fi

  stage=$(mktemp -d)
  dest="$stage/diskos-installer"
  mkdir -p "$dest"
  git -C "$ROOT" archive --format=tar "$REF" | tar -x -C "$dest"

  # required files must be committed at REF (an untracked file is NOT in the archive)
  miss=()
  for f in "${REQ_ARCHIVE[@]}"; do [ -f "$dest/$f" ] || miss+=("$f"); done
  for f in "${REQ_PAYLOAD[@]}";  do [ -f "$dest/payload/$f" ] || miss+=("payload/$f"); done
  if [ "${#miss[@]}" -gt 0 ]; then
    echo "  FAIL - required files missing from git archive of $REF (commit them first): ${miss[*]}" >&2
    rm -rf "$stage"; exit 1
  fi
  key="$dest/payload/diskos-root.pub.pem"
  if [ -L "$key" ] || grep -q 'PRIVATE KEY' "$key" ||
     [ "$(head -n 1 "$key")" != '-----BEGIN PUBLIC KEY-----' ] ||
     [ "$(tail -n 1 "$key")" != '-----END PUBLIC KEY-----' ] ||
     ! openssl pkey -pubin -in "$key" -text -noout 2>/dev/null | grep -Eq 'ASN1 OID: prime256v1|NIST CURVE: P-256'; then
    echo "  FAIL - archived payload/diskos-root.pub.pem must be a P-256 PUBLIC KEY PEM" >&2
    rm -rf "$stage"; exit 1
  fi
  # the committed payload must be the same bytes as the working-tree build the tests ran against
  for f in "${REQ_PAYLOAD[@]}"; do
    if [ -f "payload/$f" ] && ! cmp -s "payload/$f" "$dest/payload/$f"; then
      echo "  FAIL - payload/$f differs between working tree and $REF (commit or revert it)" >&2
      rm -rf "$stage"; exit 1
    fi
  done

  # only this TAG's allowlisted vendor tools; drop any other vendor content from the archive
  find "$dest/vendor" -mindepth 1 -maxdepth 1 ! -name setup-macos.sh -exec rm -rf {} +
  mkdir -p "$dest/vendor/$TAG"
  for f in "${REQ_VENDOR[@]}"; do cp -a "vendor/$TAG/$f" "$dest/vendor/$TAG/$f"; done
  if [[ "$TAG" == macos-* ]]; then
    mkdir -p "$dest/vendor/$TAG/lib"
    while IFS= read -r -d '' f; do
      name=${f##*/}
      allowed=0
      for expected in "${MACOS_DYLIBS[@]}"; do [ "$name" = "$expected" ] && allowed=1; done
      if [ "$allowed" -ne 1 ] || [ ! -f "$f" ] || [ -L "$f" ]; then
        echo "  FAIL $TAG - unexpected or non-regular library: $f" >&2
        rm -rf "$stage"; exit 1
      fi
    done < <(find "vendor/$TAG/lib" -mindepth 1 -maxdepth 1 -print0)
    for name in "${MACOS_DYLIBS[@]}"; do
      f="vendor/$TAG/lib/$name"
      if [ ! -f "$f" ] || [ -L "$f" ]; then
        echo "  FAIL $TAG - missing regular library: $f" >&2
        rm -rf "$stage"; exit 1
      fi
      cp "$f" "$dest/vendor/$TAG/lib/$name"
    done
    # Check staged Mach-O load commands, including rpaths. Linux hosts have no otool.
    for f in "$dest/vendor/$TAG/usbboot" "$dest/vendor/$TAG/mksquashfs" \
             "$dest/vendor/$TAG/unsquashfs" "$dest/vendor/$TAG/lib/"*.dylib; do
      if ! file -b "$f" | grep -q 'Mach-O'; then
        echo "  FAIL $TAG - staged native file is not Mach-O: $f" >&2
        rm -rf "$stage"; exit 1
      fi
      if command -v otool >/dev/null 2>&1; then
        refs=$(otool -l "$f") || { echo "  FAIL $TAG - otool failed: $f" >&2; rm -rf "$stage"; exit 1; }
      else
        refs=$(strings "$f") || { echo "  FAIL $TAG - strings failed: $f" >&2; rm -rf "$stage"; exit 1; }
      fi
      if grep -Eq '/opt/homebrew|/usr/local|Cellar' <<< "$refs"; then
        echo "  FAIL $TAG - staged Mach-O references Homebrew or a local library path: $f" >&2
        rm -rf "$stage"; exit 1
      fi
    done
  fi

  # forbidden-path check on the final tree
  bad=()
  for pat in "${FORBIDDEN[@]}"; do
    while IFS= read -r m; do [ -n "$m" ] && bad+=("$m"); done < <(
      case "$pat" in */*) [ -e "$dest/$pat" ] && echo "$pat";; *) find "$dest" -name "$pat" -print | sed "s|^$dest/||";; esac)
  done
  # any PEM other than the required PUBLIC OTA root key, and any file holding private-key material
  while IFS= read -r m; do bad+=("${m#$dest/}"); done < <(find "$dest" -name '*.pem' ! -path "$dest/payload/diskos-root.pub.pem")
  while IFS= read -r m; do bad+=("${m#$dest/}"); done < <(grep -rIlE -e '^-----BEGIN [A-Z ]*PRIVATE KEY-----'  "$dest" 2>/dev/null || true)
  for a in "${BLOCK_ASSETS[@]}"; do [ -e "$dest/docs/assets/$a" ] && bad+=("docs/assets/$a"); done
  if [ "${#bad[@]}" -gt 0 ]; then
    echo "  FAIL - forbidden files in the staged tree: ${bad[*]}" >&2
    rm -rf "$stage"; exit 1
  fi

  out="$REL/diskos-installer-$TAG.tar.gz"
  tar -czf "$out" -C "$stage" diskos-installer
  rm -rf "$stage"
  echo "  -> $out"
  made+=("diskos-installer-$TAG.tar.gz")
done

[ "${#made[@]}" -gt 0 ] || { echo "nothing staged." >&2; exit 1; }

echo "== generating SHA256SUMS =="
( cd "$REL" && sha256sum "${made[@]}" > SHA256SUMS ) && cat "$REL/SHA256SUMS"

NOTES="$REL/RELEASE_NOTES.md"
cat > "$NOTES" <<'EOF'
# diskOS installer - release

Installer for diskOS on the FiiO Snowsky Disc. Runs from source with your own Python (GUI + CLI). It
builds a diskOS image *from your own official FiiO firmware zip* (FiiO's rootfs is never
redistributed), flashes over mask-ROM with a bad-block-aware verify-every-block writer, and keeps a
checksum-verified stock image so **restore/remove is one action**.

## What's in the tarball
Source + the native flash tools for the platform. **No bundled Python** - you run it with your own.

## Setup
```
tar -xzf diskos-installer-<platform>.tar.gz
cd diskos-installer
./install.sh          # builds a local .venv, installs pyusb + pycryptodome
./diskos-installer doctor
```
Needs Python 3.8+. The GUI additionally needs Tk. Release bundles include the native flash and
squashfs tools; the macOS bundle includes a private libusb. On Linux, device detection needs the
system libusb-1.0 package. `install.sh` checks for missing components.

## Install / restore / remove
See the bundled `README.md`. Requires putting the device in mask-ROM (power off, hold Vol-Down, plug
USB). ~20 min; normally recoverable via mask-ROM, but not guaranteed.

## Honest status
Enthusiast flasher. Linux flashing is tested on real hardware; see the release notes for the test
status of each firmware version. macOS: image builds work, and device flashing has been verified by a
user on Apple Silicon (Intel is expected to work but is not confirmed); see the README.
Not affiliated with FiiO.
EOF
echo "  -> $NOTES"
echo "Done. Attach $REL/* to the GitHub Release."
