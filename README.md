<h1 align="center">diskOS Disco!</h1>

> **Development builds on `dev` are experimental / in testing.** Use tagged releases for the published downloads. [Development notes](docs/DEV.md).

**Disco! 3.2.1:** Smoother wheel lists, depth for Disco's buttons, idle power-off that respects the charger, a safer power-button shutdown and a Queue fix — on top of 3.2.0's wheel lists, Shade, letter wheel and round track menus. [Release notes](docs/releases/v3.2.1.md) · [Changelog](CHANGELOG.md).

<p align="center">
  <b>Your music. Your kind of Disco.</b><br>
  A touch UI for the round screen of the FiiO Snowsky Disc, built on
  <a href="https://github.com/b0hemia/diskos">b0hemia's diskOS</a>.
</p>

<p align="center">
  <img src="docs/site/zune-300.jpg" width="1000" alt="Zune theme by namastecoconut-imagined: Menu, Now Playing and Immersive 3.0">
</p>

<p align="center">
  <a href="https://github.com/zmd22/diskos-disco/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/zmd22/diskos-disco?label=Disco!&color=B36BFF&style=flat-square"></a>
  <img alt="Firmware V2.57" src="https://img.shields.io/badge/firmware-V2.57-4AD8FF?style=flat-square">
  <img alt="Status: experimental / in testing" src="https://img.shields.io/badge/status-experimental%20%2F%20in%20testing-FFB45A?style=flat-square">
  <a href="ui/COPYING"><img alt="UI GPL-3.0-or-later" src="https://img.shields.io/badge/UI-GPL--3.0--or--later-62F28C?style=flat-square"></a>
</p>

<p align="center">
  <a href="docs/DISCO_TOUR.md"><b>Tour</b></a> ·
  <a href="docs/HOWTO.md"><b>How-to</b></a> ·
  <a href="https://github.com/zmd22/diskos-disco/releases/latest"><b>Download</b></a> ·
  <a href="#install">Install</a> ·
  <a href="docs/WINDOWS.md"><b>Windows</b></a> ·
  <a href="#updates-from-the-sd-card">SD updates</a> ·
  <a href="CHANGELOG.md">Changelog</a> ·
  <a href="https://zmd22.github.io/diskos-disco/">Website</a> ·
  <a href="#thanks">Thanks</a>
</p>


---

> [!CAUTION]
> **Best-effort hobby project, experimental, as-is, no liability.** Flashing rewrites the Disc's root filesystem;
> a power cut, a bad cable or an interrupted flash can leave it unbootable. Back up your music and keep the
> saved stock image somewhere safe before you start. Not affiliated with FiiO, Snowsky or Ingenic.

## Current interface

Seven themes: Disco, Null, Squash, Zune, Ring, Braun and Stone. Browse local music, use playlists and Queue, choose List/Wall/Coverflow or supported Wheel lists, and open Immersive with lyrics and transport controls. SongSphere, Listening Stats, Album Roulette, Boomshine and Disc Shelf are built in.

See the [3.2.1 tour](docs/DISCO_TOUR.md), [actual settings](docs/DISPLAY_SETTINGS.md) and [release notes](docs/releases/v3.2.1.md). Earlier feature announcements belong in the [changelog](CHANGELOG.md).

## Install

You need a Snowsky Disc on **FiiO firmware V2.57**, FiiO's official V2.57 firmware ZIP, a good USB data cable and
about 30 quiet minutes.

> [!IMPORTANT]
> **Recommended: start from FiiO's stock V2.57 firmware.** Installing over upstream diskOS or another diskOS build is not supported and can be buggy; restore stock first ([how](#going-back)).
> Coming from upstream diskOS? Going back to stock does not clear its settings; see [Known limits](#known-limits).
> Already running diskOS Disco!? Then you don't need to reinstall: [update from the SD card](#updates-from-the-sd-card).

### From Windows

Download **[diskOS-Disco-for-Windows.zip](https://github.com/zmd22/diskos-disco/releases/latest/download/diskOS-Disco-for-Windows.zip)**,
extract it and run `diskOS-Disco.exe`. It walks you through everything, including your update keys and later SD
updates. Full guide: **[diskOS Disco! for Windows](docs/WINDOWS.md)**.

> **Windows: unsupported, but working as intended: confirmed several times by users, installs and SD updates included.** The app always installs the latest release (or an `mq_ui` you pick).
> Start from FiiO's stock V2.57 firmware, not another diskOS build.
>
> **Used the Windows app before? Set it up again once.** Download the latest Windows app, open it and click
> **Set up again** on its setup page. Until then, an older app keeps installing the version it was first set up with.
> From 2.5.9 on, the app also updates itself to the latest release on every start.

### From macOS

Follow the **[macOS installation guide](docs/MACOS.md)**. Apple Silicon Macs can use the ready-built native flash tools from b0hemia/diskOS 1.2.0 with the Disco installer. Intel Macs build the tools once.

### From Linux

The installer is upstream diskOS's (`./diskos-installer --help`).

**0. Get it.** Download the **[latest release](https://github.com/zmd22/diskos-disco/releases/latest)** (Source
code, tar.gz) and unpack it, or `git clone https://github.com/zmd22/diskos-disco`. The ready-built interface is
already in it as `payload/mq_ui`; the release also has `mq_ui` on its own, with checksums, for SD-card
updates.

**1. Make your update keys (once, optional but recommended).** They let you install new builds from the SD card
later. Skip this and leave out `--ota-key` below if you only want to flash.

```sh
sh ui/tools/owner-keys/diskos-keys.sh          # creates ./diskos-keys/  (keep the private keys safe)
cp diskos-keys/diskos-root.pub.pem payload/
```

**2. Flash the public variant with Disco! and your key**

```sh
./install.sh                                   # first time only: host setup
./diskos-installer install \
  --firmware SNOWSKY_DISC_update_20260909_v257.zip \
  --variant public \
  --ui payload/mq_ui \
  --ota-key payload/diskos-root.pub.pem
```

Power the Disc off, hold **Volume Down** and plug in USB (the screen stays black). If you made keys, check the
summary line `ota: ON … key sha256 xxxx` before you answer `y`. It must match `sha256sum payload/diskos-root.pub.pem`.

> [!TIP]
> Use **public**, not dev. The dev variant's USB serial console takes the Disc's only USB device port, which
> blocks USB storage mode. Settings › System › Maintenance › Debug Mode still gives you SSH when you need it.

## Updates from the SD card

**On Windows:** the app's **Update** page downloads the latest version, signs it with your keys and writes it to the
SD card ([how](docs/WINDOWS.md#step-4--update-from-the-sd-card)). **On Linux:** download `mq_ui` from the [latest release](https://github.com/zmd22/diskos-disco/releases/latest) (or build it), then sign it with
your keys:

```sh
sh ui/tools/owner-keys/diskos-sign.sh path/to/mq_ui 3.2.1      # -> ./diskos-update/ (the label is free text)
```

Reuse the signing keys whose root public key was installed on this Disc. Copy the complete
`diskos-update` **folder** to the SD card root and safely eject the storage connection. On the Disc,
go to **Settings › System › Maintenance › Update from SD Card › Update**, wait for staging, then restart.

The new interface runs as a **trial**. Check Settings › Help › About and leave it running for at
least three minutes. Confirmed playback can accept it automatically; otherwise choose **Keep**
when prompted, or **Go back** to revert. Restart after acceptance to confirm the kept build.
An unproven or failing trial rolls back. SD updates change the interface only, not the optional apps.
The **[full SD update guide](docs/UPDATING.md)** covers first-time keys, verification, folder contents,
epoch ordering, trial acceptance and troubleshooting.

## Build it yourself

Use the [current UI guide](docs/UI.md) and [build workflow](docs/DEV_WORKFLOW.md). Build with the pinned MIPS toolchain and vendored LVGL; keep native previews and device objects separate. Record the source revision, ABI and payload hash. A successful build is not proof of a byte-identical release or physical-device behaviour.

## Known limits

- **Testing status.** Each release says what was tried on a real Disc and what was only checked on a computer.
  The newest features of a release usually need some days of everyday use; please report what you find.
- **Start from stock.** Install on FiiO's stock V2.57 firmware. Installing over upstream diskOS or another diskOS
  build is not supported and can be buggy.
- **Disc keeps starting stock after installing?** A "Default UI: Stock" choice made earlier (in Disco or upstream
  diskOS) is kept in the Disc's data area, which flashing and firmware updates don't erase, so every new install still
  starts stock. Hold **Volume Up** while powering on to start Disco once, then set **Settings > System > Controls >
  Default UI** to **diskOS**. No need to flash again.
- **Switching between diskOS builds.** Switched to Disco from upstream diskOS or another build, and something looks or behaves wrong? **Settings > System > Maintenance > Reset diskOS Settings** gives the interface a fresh start; your music, Wi-Fi, Bluetooth, EQ and audio settings stay. Going back to stock in between does *not* clear what diskOS leaves on the Disc.
- **Windows:** Unsupported, but working as intended: confirmed several times by users, installs and SD updates included.
- **Firmware.** Made for FiiO firmware V2.57 only.
- **Cover cache.** Covers for Cover Flow and lists are cached on the SD card only while it has enough free space
  (500 MB). On a nearly full card, covers may stay blank.
- **If you suddenly see FiiO's own interface,** Disco! stopped and the Disc fell back to the stock one; restart to
  return. Over SSH (Debug Mode), `/usr/data/diskos_boot.log` and `/usr/data/diskos_install.log` show why.
- **Languages.** Titles, artists and lyrics can be in Latin-script languages (including Vietnamese), Greek,
  Cyrillic, Thai, Korean, Japanese and common Chinese characters. Chinese, Japanese and Korean show in one smaller
  size. Right-to-left scripts (Hebrew, Arabic) and Indian scripts are not supported yet.
- **No online updates.** New versions come by signed SD-card update or a fresh install.

## Going back

Stock is never far:

- **Settings › System › Controls › Default UI › Stock** makes FiiO's interface the normal choice.
- Hold **Volume Up** from power-on to boot the other interface once.
- `./diskos-installer restore-stock` (Linux) puts the stock system back, using the backup made at install. Disco's
  settings stay in the Disc's data area (unused), so a later reinstall comes back with them. The Windows app keeps
  the backup in `%LOCALAPPDATA%\diskOS-Disco\backup`; keep a copy of it elsewhere too.

**What survives what** (flashing, restoring stock, FiiO updates, switching to upstream diskOS), and the fixes for the
usual surprises: [Flashing and switching interfaces](docs/SWITCHING.md).

## Documentation

| | |
|---|---|
| [Flashing and switching interfaces](docs/SWITCHING.md) | What stays and what goes when you flash, restore stock or switch |
| [Tour](docs/DISCO_TOUR.md) | Current screens, controls and built-in apps |
| [How-to](docs/HOWTO.md) | Short answers for everyday tasks |
| [Updating](docs/UPDATING.md) | Keys, signing, flashing once, troubleshooting |
| [macOS](docs/MACOS.md) | Install Disco on Apple Silicon or Intel Macs |
| [Windows](docs/WINDOWS.md) | diskOS Disco! for Windows: install and update from a Windows PC |
| [Development targets](todo.md) | Proposed work and hardware validation |
| [Changelog](CHANGELOG.md) | Every Disco! release, newest first |
| [All docs](docs/README.md) | Optional apps, hardware notes, developer workflow, legal |

## A note from zmd22

I’m out. Peace, and happy listening. Thank you to everyone who built, tested, shared and listened. Enjoy the music.

## Thanks

- **[namastecoconut-imagined](https://github.com/namastecoconut-imagined/diskos-disco-zune)**, for the original Zune theme: its Metro-inspired direction, typography, home menu, music screen and library presentation. The Disco adaptation builds on that design and implementation; zmd22’s follow-up focuses on integration, readability and controls. [Original author’s commits](https://github.com/namastecoconut-imagined/diskos-disco-zune/commits?author=namastecoconut-imagined).

- **[b0hemia](https://github.com/b0hemia/diskos)**, for diskOS itself: the reverse engineering, the installer,
  the boot and recovery chain, the signed-update design and the whole UI this project grew from. None of this
  exists without that work. If you enjoy diskOS, [support b0hemia on Ko-fi](https://ko-fi.com/b0hemia).
- **diskOS contributors**: everyone who reported, tested and patched upstream.
- **[ingvarr777](https://github.com/ingvarr777)**, for Ogg Opus support (diskOS PR #19) and the careful bug reports
  behind 1.4.1 (issues #1–#8).
- **[LVGL](https://lvgl.io)** for the graphics library, **SQLite**, **jsmn**, **musl**,
  **[musl-cross-make](https://github.com/richfelker/musl-cross-make)**, the **Inter**, **Montserrat** and
  **Font Awesome** fonts, and every project listed in [NOTICE.md](NOTICE.md).
- **Dieter Rams** and the Braun SK4, for the design language the Braun theme borrows with respect.
- FiiO / Snowsky for a lovely little round player. This project is **not affiliated with or endorsed by FiiO,
  Snowsky or Ingenic**.

### A note on the contributor list

Early commits in this repository show an author called "disco" because of a misconfigured author email; they
were made by the project's maintainer ([zmd22](https://github.com/zmd22)). The history is left as it is, since
rewriting it would break tags, links and existing clones.

## License

The installer, scripts and docs are **MIT** ([LICENSE](LICENSE)). The on-device interface in [ui/](ui/) is
**GPL-3.0-or-later** ([ui/COPYING](ui/COPYING)). Third-party terms are in [NOTICE.md](NOTICE.md),
[SPL_SOURCE.md](SPL_SOURCE.md) and [licenses/](licenses/).

> [!WARNING]
> Never share generated `diskos_*.bin` images: they contain FiiO's root filesystem. Share the installer and let
> people build from their own firmware.
