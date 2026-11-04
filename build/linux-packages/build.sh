#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 diskOS contributors
#
# Build the Fedora (.rpm) and Arch (.pkg.tar.zst) packages of the diskOS Disco! Linux app, each in its own distro's
# container, into build/linux-packages/out/. Needs podman and the static tools in vendor/linux-x86_64.
#   build/linux-packages/build.sh [fedora] [arch]      (default: both)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd); REPO=$(cd "$HERE/../.." && pwd)
OUT="$HERE/out"; mkdir -p "$OUT"
VER=$(sed -n 's/^#define DISCO_VERSION[[:space:]]*"\([^"]*\)".*/\1/p' "$REPO/ui/version.h" | tr '-' '_')
[ -n "$VER" ] || { echo "no DISCO_VERSION in ui/version.h" >&2; exit 1; }
[ "$#" -gt 0 ] || set -- fedora arch
for d in "$@"; do
  case "$d" in
  fedora)
    podman run --rm --network=host -v "$REPO:/repo:ro,Z" -v "$OUT:/out:Z" registry.fedoraproject.org/fedora:latest sh -c "
      dnf -y -q install rpm-build python3 >/dev/null &&
      rpmbuild -bb --define 'disco_version $VER' --define 'repo /repo' --define '_rpmdir /tmp/rpm' \
        /repo/build/linux-packages/diskos-disco.spec >/tmp/rpm.log 2>&1 || { tail -30 /tmp/rpm.log; exit 1; }
      cp /tmp/rpm/x86_64/*.rpm /out/" ;;
  arch)
    podman run --rm --network=host -v "$REPO:/repo:ro,Z" -v "$OUT:/out:Z" docker.io/archlinux/archlinux:latest sh -c "
      pacman -Sy --noconfirm --needed base-devel python >/dev/null 2>&1 &&
      useradd -m b && mkdir /home/b/pkg && cp /repo/build/linux-packages/PKGBUILD /repo/build/linux-packages/diskos-disco.install /home/b/pkg/ &&
      chown -R b /home/b/pkg && cd /home/b/pkg &&
      su b -c 'DISCO_REPO=/repo DISCO_VERSION=$VER PKGDEST=/home/b/pkg makepkg --nodeps --noconfirm' >/tmp/mk.log 2>&1 || { tail -30 /tmp/mk.log; exit 1; }
      cp /home/b/pkg/*.pkg.tar.zst /out/" ;;
  *) echo "unknown target: $d (fedora|arch)" >&2; exit 2 ;;
  esac
done
ls -la "$OUT"
