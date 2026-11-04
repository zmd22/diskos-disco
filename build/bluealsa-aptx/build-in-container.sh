#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 diskOS contributors
#
# Build the opt-in aptX BlueALSA for the Disc: upstream BlueALSA 4.0.0 (MIT, Arkadiusz Bokowy) - the version stock
# V2.57 runs - with aptX and aptX HD enabled alongside SBC, AAC and LDAC. Runs INSIDE the toolchain image
# (Containerfile.toolchain: crosstool-ng, glibc 2.29, MIPS32r2 LE, nan2008 - the Disc's ABI). See build.sh.
#
# aptX / aptX HD come from libfreeaptx 0.1.1 (LGPL-2.1, a standalone encoder), built here and shipped NEXT TO
# bluealsa as libfreeaptx.so.0 (found through RPATH=$ORIGIN): a separate, replaceable library, as the LGPL asks.
# The Disc's own libaptx/libaptxHD can't be used: they wrap FFmpeg, and FiiO's FFmpeg has no aptX encoder
# ("ffmpeg apt-X: Encoder not found", tested on the Disc) - playing over them rebooted the Disc.
# Everything else links dynamically against the Disc's OWN libraries (mounted read-only at /sysroot, copied off a
# V2.57 Disc): libldacBT_enc/_abr, libfdk-aac, libsbc, GLib/GIO, libbluetooth, libasound. Nothing from /sysroot is
# copied into the output. Headers only (the Disc has none): Debian 10 "buster" mipsel -dev packages (GLib 2.58,
# D-Bus, BlueZ, SBC, ALSA - the same or older than the Disc's libraries), upstream fdk-aac 2.0.1 and ldacBT 2.0.2.3.
set -eu
X=/home/b/x-tools/mipsel-disc-linux-gnu
CROSS=$X/bin/mipsel-disc-linux-gnu-
SYSROOT=/sysroot
W=/home/b/bluealsa-build; ST=$W/stage; PC=$W/pc
rm -rf "$W"; mkdir -p "$W" "$ST" "$PC"; cd "$W"

# --- headers: Debian buster mipsel -dev packages ------------------------------------------------------------------
DEB=http://archive.debian.org/debian
wget -q "$DEB/dists/buster/main/binary-mipsel/Packages.gz"
for p in libglib2.0-dev libdbus-1-dev libbluetooth-dev libsbc-dev libasound2-dev libbsd-dev; do
    f=$(zcat Packages.gz | awk -v p="$p" '$1=="Package:"{x=($2==p)} x&&$1=="Filename:"{print $2; exit}')
    [ -n "$f" ] || { echo "no $p in buster/mipsel" >&2; exit 1; }
    wget -q "$DEB/$f" -O "$p.deb" && dpkg-deb -x "$p.deb" "$ST"
done
find "$ST" -name '*.so' -type l -delete                     # never link Debian's libraries: only the Disc's
find "$ST" -name '*.a' -delete

# --- headers: upstream codec libraries at the Disc's versions -----------------------------------------------------
wget -q -O fdk.tgz https://github.com/mstorsjo/fdk-aac/archive/refs/tags/v2.0.1.tar.gz && tar xzf fdk.tgz
mkdir -p "$ST/usr/include/fdk-aac"
cp fdk-aac-2.0.1/libAACenc/include/aacenc_lib.h fdk-aac-2.0.1/libAACdec/include/aacdecoder_lib.h \
   fdk-aac-2.0.1/libSYS/include/FDK_audio.h fdk-aac-2.0.1/libSYS/include/genericStds.h \
   fdk-aac-2.0.1/libSYS/include/machine_type.h fdk-aac-2.0.1/libSYS/include/syslib_channelMapDescr.h "$ST/usr/include/fdk-aac/"
wget -q -O ldac.tgz https://github.com/EHfive/ldacBT/releases/download/v2.0.2.3/ldacBT-2.0.2.3.tar.gz && tar xzf ldac.tgz
mkdir -p "$ST/usr/include/ldac"
cp ldacBT/libldac/inc/ldacBT.h ldacBT/libldac/abr/inc/ldacBT_abr.h "$ST/usr/include/ldac/"

# --- libfreeaptx 0.1.1 (LGPL-2.1): built for the Disc, shipped beside bluealsa ---------------------------------------
FA=libfreeaptx-0.1.1
wget -q -O "$FA.tar.gz" https://github.com/iamthehorker/libfreeaptx/archive/refs/tags/0.1.1.tar.gz && tar xzf "$FA.tar.gz"
mkdir -p "$ST/usr/lib"
"${CROSS}gcc" -O2 -mips32r2 -mnan=2008 -mfp64 -fPIC -shared -Wl,-soname,libfreeaptx.so.0 -I"$FA" "$FA/freeaptx.c" \
    -o "$ST/usr/lib/libfreeaptx.so.0"
ln -sf libfreeaptx.so.0 "$ST/usr/lib/libfreeaptx.so"
cp "$FA/freeaptx.h" "$ST/usr/include/"
# BlueALSA 4.0's --with-libopenaptx expects pali's libopenaptx; libfreeaptx is its continuation with the same API
printf '#include <freeaptx.h>\n' > "$ST/usr/include/openaptx.h"

# Paths are sysroot-relative: pkg-config prefixes PKG_CONFIG_SYSROOT_DIR (the header stage) to -I. Library search paths
# come from LDFLAGS (the Disc's own /usr/lib), never from here.
pc(){ printf '%sName: %s\nDescription: %s (Disc)\nVersion: %s\nLibs: %s\nCflags: -I/usr/include%s\n' \
        "$5" "$1" "$1" "$2" "$3" "$4" > "$PC/$1.pc"; }
# GLib's .pc lists private dependencies (pcre, ffi, zlib, mount, selinux...) that a shared link never uses but
# pkg-config insists on finding: empty stubs (the Disc's own libraries resolve them at run time).
for d in libpcre libffi zlib mount blkid uuid libselinux libsepol libpcre2-8 gmodule-no-export-2.0; do pc "$d" 99 "" "" ""; done
pc fdk-aac      2.0.1   -lfdk-aac      ""          ""
pc ldacBT-enc   2.0.2.3 -lldacBT_enc   /ldac       ""
pc ldacBT-abr   2.0.2.3 -lldacBT_abr   /ldac       ""
pc libopenaptx  0.2.1   "-L/usr/lib -lfreeaptx" "" ""          # -L is under the header stage: our libfreeaptx

# --- BlueALSA 4.0.0 ---------------------------------------------------------------------------------------------------
wget -q -O ba.tgz https://github.com/arkq/bluez-alsa/archive/refs/tags/v4.0.0.tar.gz && tar xzf ba.tgz
cd bluez-alsa-4.0.0
for p in /patches/*.patch; do echo "patch: $(basename "$p")"; patch -p1 < "$p" >/dev/null; done   # build/bluealsa-aptx/patches
autoreconf -fi >/dev/null 2>&1
export PKG_CONFIG_SYSROOT_DIR="$ST"
export PKG_CONFIG_LIBDIR="$ST/usr/lib/mipsel-linux-gnu/pkgconfig:$ST/usr/share/pkgconfig:$ST/usr/lib/pkgconfig"
export PKG_CONFIG_PATH="$PC"
./configure --host=mipsel-disc-linux-gnu --prefix=/usr \
    CC="${CROSS}gcc" \
    CFLAGS="-O2 -mips32r2 -mnan=2008 -mfp64 -I$ST/usr/lib/mipsel-linux-gnu/glib-2.0/include -ffile-prefix-map=$W/=" \
    LDFLAGS="-L$SYSROOT/usr/lib -L$SYSROOT/lib -Wl,-rpath-link,$SYSROOT/lib -Wl,-rpath-link,$SYSROOT/usr/lib"' -Wl,-rpath,\$$ORIGIN' \
    --enable-aac --enable-aptx --enable-aptx-hd --enable-ldac --with-libopenaptx \
    --disable-aplay --disable-cli --disable-rfcomm --disable-hcitop --disable-manpages --disable-test --disable-systemd \
    > configure.out 2>&1 || { tail -25 configure.out; exit 1; }
grep -E 'define (ENABLE_(AAC|APTX|APTX_HD|LDAC)|HAVE_(APTX|APTX_HD|LDAC)_DECODE)' config.h
make -j"$(nproc)" -C src bluealsa > make.out 2>&1 || { tail -40 make.out; exit 1; }
mkdir -p /out
# glibc needs >= 4.5 kernel headers for MIPS nan2008, so the toolchain's ABI note says 4.x; the Disc (kernel 4.4.94)
# runs its own glibc (FiiO's, built "for GNU/Linux 3.10.14"). Drop the note so nothing reads a minimum from it.
"${CROSS}strip" --remove-section=.note.ABI-tag -o /out/bluealsa src/bluealsa
"${CROSS}strip" --remove-section=.note.ABI-tag -o /out/libfreeaptx.so.0 "$ST/usr/lib/libfreeaptx.so.0"
"${CROSS}readelf" -d /out/bluealsa | grep -E "NEEDED|RPATH|RUNPATH"
"${CROSS}readelf" -d /out/bluealsa | grep -q -E '(RPATH|RUNPATH).*\$ORIGIN' || { echo "bluealsa won't find libfreeaptx beside it (no \$ORIGIN RPATH/RUNPATH)" >&2; exit 1; }
"${CROSS}readelf" -d /out/bluealsa | grep -q 'libfreeaptx.so.0' || { echo "bluealsa isn't linked to libfreeaptx" >&2; exit 1; }
! "${CROSS}readelf" -d /out/bluealsa | grep -q 'libaptx' || { echo "bluealsa still links the Disc's broken libaptx" >&2; exit 1; }
# The Disc is FP64 ("32-bit CPU, 64-bit FPU": its loader and every library). The kernel refuses an FP32 program
# outright ("Accessing a corrupted shared library"), so check the ABI flags here, not on the Disc.
fp=$("${CROSS}readelf" -A /out/bluealsa | sed -n 's/.*FP ABI: *//p')
want=$("${CROSS}readelf" -A /sysroot/lib/ld-linux-mipsn8.so.1 | sed -n 's/.*FP ABI: *//p')
echo "FP ABI: $fp (Disc loader: $want)"
[ "$fp" = "$want" ] || { echo "FP ABI mismatch: built '$fp', the Disc needs '$want'" >&2; rm -f /out/bluealsa; exit 1; }
fpl=$("${CROSS}readelf" -A /out/libfreeaptx.so.0 | sed -n 's/.*FP ABI: *//p')
[ "$fpl" = "$want" ] || { echo "FP ABI mismatch in libfreeaptx: '$fpl'" >&2; exit 1; }
cp ../bluez-alsa-4.0.0/LICENSE /out/LICENSE-bluez-alsa.txt
cp "../$FA/COPYING" /out/LICENSE-libfreeaptx.txt                  # LGPL-2.1 text
cp "../$FA.tar.gz" /out/                                          # its exact corresponding source
echo "built: /out/bluealsa"
