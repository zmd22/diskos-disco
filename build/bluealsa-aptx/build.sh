#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 diskOS contributors
#
# Build the opt-in aptX BlueALSA (see build-in-container.sh for what and why).
#   SYSROOT=~/disc-sysroot-v257 build/bluealsa-aptx/build.sh [outdir]
# SYSROOT: a copy of a V2.57 Disc's /lib and /usr/lib (e.g. ssh root@disc 'tar -cf - /lib /usr/lib' | tar -xf - -C ...).
# Vendor libraries stay out of the repo; the output is only BlueALSA itself plus its MIT license text.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
SYSROOT=${SYSROOT:?set SYSROOT to a copy of the Disc /lib and /usr/lib}
OUT=${1:-"$HERE/out"}
mkdir -p "$OUT"
podman image exists disc-xtools-glibc229 || podman build --network=host -t disc-xtools-glibc229 -f "$HERE/Containerfile.toolchain" "$HERE"
podman run --rm --network=host --userns=keep-id \
    -v "$SYSROOT:/sysroot:ro,Z" -v "$OUT:/out:Z" -v "$HERE/build-in-container.sh:/build.sh:ro,Z" -v "$HERE/patches:/patches:ro,Z" \
    disc-xtools-glibc229 sh /build.sh
