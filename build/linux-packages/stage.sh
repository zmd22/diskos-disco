#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 diskOS contributors
#
# Stage the diskOS Disco! Linux app into a package root (used by the Fedora .spec and the Arch PKGBUILD):
#   /opt/diskos-disco/                 the installer, payload (mq_ui), owner-key tools, static flash tools
#   /usr/bin/diskos-disco              launcher (GUI with no arguments; CLI subcommands pass through)
#   /usr/share/applications/…desktop   menu entry, no terminal
#   /usr/share/icons/hicolor/…         the disc icon
#   /usr/lib/udev/rules.d/…rules       USB access to the Disc in flash mode, for the logged-in user
#
#   build/linux-packages/stage.sh <repo> <destdir>
# The static usbboot/mksquashfs/unsquashfs must already be in <repo>/vendor/linux-x86_64 (build-*-static.sh).
set -eu
SRC=$(cd "$1" && pwd); DEST=$2
APP="$DEST/opt/diskos-disco"
V="$SRC/vendor/linux-x86_64"
for f in usbboot mksquashfs unsquashfs my_write6_dram.bin disc_spl_lpddr3.bin; do
    [ -f "$V/$f" ] || { echo "missing vendor/linux-x86_64/$f (run build/build-usbboot-static.sh and build-squashfs-static.sh)" >&2; exit 1; }
done
for f in mq_ui mq_ui.build.json; do [ -f "$SRC/payload/$f" ] || { echo "missing payload/$f" >&2; exit 1; }; done

mkdir -p "$APP/vendor/linux-x86_64" "$APP/ui/tools/owner-keys" "$APP/udev" \
         "$DEST/usr/bin" "$DEST/usr/share/applications" "$DEST/usr/share/icons/hicolor/256x256/apps" \
         "$DEST/usr/lib/udev/rules.d" "$DEST/usr/share/licenses/diskos-disco"
cp "$SRC/diskos-installer" "$APP/"
( cd "$SRC" && find diskos_installer -name '*.py' ) | while read -r f; do
    install -D -m 0644 "$SRC/$f" "$APP/$f"
done
cp -r "$SRC/payload" "$APP/"
rm -f "$APP/payload/"*.key "$APP/payload/"*.squashfs 2>/dev/null || true      # never ship a private key or image
for f in usbboot mksquashfs unsquashfs; do install -m 0755 "$V/$f" "$APP/vendor/linux-x86_64/$f"; done
for f in my_write6_dram.bin disc_spl_lpddr3.bin; do install -m 0644 "$V/$f" "$APP/vendor/linux-x86_64/$f"; done
install -m 0755 "$SRC/ui/tools/owner-keys/diskos-keys.sh" "$SRC/ui/tools/owner-keys/diskos-sign.sh" "$APP/ui/tools/owner-keys/"
install -m 0644 "$SRC/udev/70-diskos-maskrom.rules" "$APP/udev/"
install -m 0644 "$SRC/udev/70-diskos-maskrom.rules" "$DEST/usr/lib/udev/rules.d/70-diskos-maskrom.rules"
cp "$SRC/LICENSE" "$SRC/NOTICE.md" "$APP/"
cp -r "$SRC/licenses" "$APP/"
cp "$SRC/LICENSE" "$SRC/NOTICE.md" "$DEST/usr/share/licenses/diskos-disco/"
# GPL tools (usbboot, squashfs-tools): their corresponding source ships next to them, as NOTICE.md describes.
[ -d "$SRC/corresponding-source" ] && cp -r "$SRC/corresponding-source" "$APP/" || true
find "$APP" -name '__pycache__' -prune -exec rm -rf {} +

cat > "$DEST/usr/bin/diskos-disco" <<'EOF'
#!/bin/sh
# diskOS Disco! - the graphical installer by default; any argument is a diskos-installer command (e.g. doctor).
exec /opt/diskos-disco/diskos-installer "${@:-gui}"
EOF
chmod 0755 "$DEST/usr/bin/diskos-disco"

cat > "$DEST/usr/share/applications/diskos-disco.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=diskOS Disco!
GenericName=Disc installer
Comment=Install diskOS Disco! on the Snowsky Disc and make SD-card updates
Exec=diskos-disco
Icon=diskos-disco
Terminal=false
Categories=Utility;AudioVideo;
StartupWMClass=Diskos-disco
EOF

PYTHONPATH="$SRC" python3 -c "from diskos_installer.app import disc_png; open('$DEST/usr/share/icons/hicolor/256x256/apps/diskos-disco.png','wb').write(disc_png(256))"
echo "staged into $DEST"
