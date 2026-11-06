<h1 align="center">diskOS Disco!</h1>

> `dev` is experimental. Use tagged releases. [Dev notes](docs/DEV.md)

**3.4.1:** Sendspin 1.0 (encryption, code pairing, album art, experimental FLAC), new Bluetooth/Wi-Fi screens for Squash and Disco, shuffle fixes. [Release notes](docs/releases/v3.4.1.md) · [Bluetooth](docs/BLUETOOTH.md) · [Changelog](CHANGELOG.md)

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
  <a href="docs/LINUX.md"><b>Linux</b></a> ·
  <a href="docs/BLUETOOTH.md">Bluetooth</a> ·
  <a href="#updates-from-the-sd-card">SD updates</a> ·
  <a href="CHANGELOG.md">Changelog</a> ·
  <a href="https://zmd22.github.io/diskos-disco/">Website</a> ·
  <a href="#thanks">Thanks</a>
</p>


---

> [!CAUTION]
> Hobby project, provided as-is, no liability. An interrupted flash can brick the Disc: back up your music and keep the
> stock image. Not affiliated with FiiO, Snowsky or Ingenic.

## Features

- 8 themes: Disco, Null, Squash, Zune, Braun 2.0, Ring, Braun, Stone
- Local library, playlists, Queue; List, Wall, Coverflow and Wheel views; Immersive mode with lyrics
- Built-in apps: SongSphere, Listening Stats, Album Roulette, Boomshine, Disc Shelf
- Bluetooth: SBC, AAC, LDAC, aptX / aptX HD ([experimental](docs/BLUETOOTH.md))

See the [tour](docs/DISCO_TOUR.md) and [settings](docs/DISPLAY_SETTINGS.md).

## Install

Requires: a Disc on **FiiO firmware V2.57**, the V2.57 firmware ZIP, a USB data cable.

> [!IMPORTANT]
> Install over stock V2.57, not upstream diskOS or another build ([back to stock](#going-back)).
> Already on Disco!? [Update from the SD card](#updates-from-the-sd-card).

### Windows

Download **[diskOS-Disco-for-Windows.zip](https://github.com/zmd22/diskos-disco/releases/latest/download/diskOS-Disco-for-Windows.zip)**, extract, run `diskOS-Disco.exe`. Tabs: Check, Setup, Install, Update, Restore. Unsupported but user-confirmed; upgrading from an older app, click **Set up again** once. [Guide](docs/WINDOWS.md)

### macOS

See the [macOS guide](docs/MACOS.md).

### Linux

**App (experimental):** install the Fedora `.rpm` or Arch `.pkg.tar.zst` from the [latest release](https://github.com/zmd22/diskos-disco/releases/latest), or run `./diskos-installer` from source. [Guide](docs/LINUX.md)

**CLI:** from the release source or a clone (`payload/mq_ui` is prebuilt):

```sh
sh ui/tools/owner-keys/diskos-keys.sh          # once: update keys in ./diskos-keys/ (keep them safe; optional)
cp diskos-keys/diskos-root.pub.pem payload/
./install.sh                                   # first time only: host setup
./diskos-installer install \
  --firmware SNOWSKY_DISC_update_20260909_v257.zip \
  --variant public \
  --ui payload/mq_ui \
  --ota-key payload/diskos-root.pub.pem         # leave out without keys
```

With the Disc off, hold **Volume Down** and plug in USB (screen stays black). With keys, confirm `ota: ON … key sha256` matches `sha256sum payload/diskos-root.pub.pem` before answering `y`. Use `public`; `dev` disables USB storage.

## Updates from the SD card

**Windows / Linux app:** use the Update tab ([Windows](docs/WINDOWS.md#update-from-the-sd-card), [Linux](docs/LINUX.md)). **CLI:** sign the release `mq_ui` with the keys installed on the Disc:

```sh
sh ui/tools/owner-keys/diskos-sign.sh path/to/mq_ui 3.4.1      # -> ./diskos-update/
```

Copy the six files to `<SD>/diskos-update/`, overwriting, then **Settings › System › Maintenance › Update from SD Card › Update** and restart. The update boots as a trial: **Keep** (or confirmed playback) accepts it, **Go back** or a failure rolls back. [Details](docs/UPDATING.md)

## Build it yourself

[UI guide](docs/UI.md) · [Build workflow](docs/DEV_WORKFLOW.md)

## Known limits

- Firmware V2.57 only.
- Boots stock every time: hold **Volume Up** at power-on, then set **Settings › System › Controls › Default UI** to **diskOS**.
- Odd behaviour after another diskOS build: **Settings › System › Maintenance › Reset diskOS Settings** (keeps music, Wi-Fi, Bluetooth, audio).
- Covers are cached only with ≥500 MB free on the SD card.
- FiiO UI appears unexpectedly: Disco! exited; restart. Reason is in `/usr/data/diskos_boot.log`.
- Scripts: Latin, Greek, Cyrillic, Thai, Korean, Japanese, common Chinese. No RTL or Indic.
- Updates via signed SD update or reinstall only; no OTA.
- Bluetooth codecs are experimental. Press Play after switching headphones. No aptX Adaptive/Lossless. [More](docs/BLUETOOTH.md#known-limits)

## Going back

- **Settings › System › Controls › Default UI › Stock** sets FiiO's UI as default; **Volume Up** at power-on boots the other once.
- `./diskos-installer restore-stock` restores the install-time backup. Windows stores it in `%LOCALAPPDATA%\diskOS-Disco\backup`; copy it elsewhere.
- What survives each path: [Flashing and switching](docs/SWITCHING.md).

## Documentation

| | |
|---|---|
| [Tour](docs/DISCO_TOUR.md) | Screens, controls and built-in apps |
| [How-to](docs/HOWTO.md) | Everyday tasks |
| [Updating](docs/UPDATING.md) | Keys, signing, troubleshooting |
| [Flashing and switching](docs/SWITCHING.md) | What stays when you flash or switch |
| [Windows](docs/WINDOWS.md) · [Linux](docs/LINUX.md) · [macOS](docs/MACOS.md) | Installing from a PC or Mac |
| [Bluetooth](docs/BLUETOOTH.md) | Codecs, aptX, Quiet Wi-Fi (experimental) |
| [Changelog](CHANGELOG.md) | Every release, newest first |
| [All docs](docs/README.md) | Apps, hardware notes, development, legal |

## A note from zmd22

I’m out. Peace, and happy listening. Thank you to everyone who built, tested, shared and listened. Enjoy the music.

## Thanks

- **[namastecoconut-imagined](https://github.com/namastecoconut-imagined/diskos-disco-zune)**: the original Zune theme Disco's builds on.
- **[b0hemia](https://github.com/b0hemia/diskos)**: diskOS — reverse engineering, installer, boot/recovery chain, signed updates, the base UI. [Support b0hemia on Ko-fi](https://ko-fi.com/b0hemia).
- **diskOS contributors**: upstream reports, testing and patches.
- **[ingvarr777](https://github.com/ingvarr777)**: Ogg Opus support, bug reports.
- **[LVGL](https://lvgl.io)**, SQLite, jsmn, musl, Inter, Montserrat, Font Awesome and everything in [NOTICE.md](NOTICE.md).
- **Dieter Rams** and the Braun SK4, inspiration for the Braun theme.
- FiiO / Snowsky for the hardware (no affiliation).

Early commits by "disco" are the maintainer ([zmd22](https://github.com/zmd22)) under a misconfigured email.

## License

Installer, scripts, docs: **MIT** ([LICENSE](LICENSE)). [ui/](ui/): **GPL-3.0-or-later** ([COPYING](ui/COPYING)). Third-party: [NOTICE.md](NOTICE.md), [SPL_SOURCE.md](SPL_SOURCE.md), [licenses/](licenses/).

> [!WARNING]
> Don't share generated `diskos_*.bin` images; they contain FiiO's root filesystem.
