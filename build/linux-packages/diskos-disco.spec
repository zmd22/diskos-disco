# SPDX-License-Identifier: MIT
# Copyright (c) 2026 diskOS contributors
# Fedora package of the diskOS Disco! Linux app. Built by build/linux-packages/build.sh (in a Fedora container).
Name:           diskos-disco
Version:        %{disco_version}
Release:        1%{?dist}
Summary:        Install diskOS Disco! on the Snowsky Disc and make SD-card updates
License:        MIT AND GPL-2.0-or-later AND GPL-3.0-or-later
URL:            https://github.com/zmd22/diskos-disco
ExclusiveArch:  x86_64
Requires:       python3 >= 3.8
Requires:       python3-tkinter
Requires:       python3-pyusb
Requires:       python3-pycryptodomex
Requires:       openssl
Requires:       xdg-utils
Requires:       polkit
# The bundled static flash tools and the Disc's MIPS mq_ui must not be touched by the build root policies.
%global __os_install_post %{nil}
%global debug_package %{nil}
%global __requires_exclude_from ^/opt/diskos-disco/(payload|vendor)/.*$
%global __provides_exclude_from ^/opt/diskos-disco/.*$

%description
The diskOS Disco! installer for Linux, with a graphical app: flashes diskOS Disco! onto the FiiO Snowsky Disc
from FiiO's own firmware, makes your update keys, and signs SD-card updates. Includes static usbboot and
squashfs-tools and the udev rule that lets your user reach the Disc in flash mode.

%install
sh %{repo}/build/linux-packages/stage.sh %{repo} %{buildroot}

%post
udevadm control --reload-rules >/dev/null 2>&1 || :
udevadm trigger --subsystem-match=usb >/dev/null 2>&1 || :

%files
/opt/diskos-disco
%{_bindir}/diskos-disco
%{_datadir}/applications/diskos-disco.desktop
%{_datadir}/icons/hicolor/256x256/apps/diskos-disco.png
%{_prefix}/lib/udev/rules.d/70-diskos-maskrom.rules
%license %{_datadir}/licenses/diskos-disco/LICENSE
%{_datadir}/licenses/diskos-disco/NOTICE.md
