<h1 align="center">diskOS Disco!</h1>

> **Development builds on `dev` are experimental / in testing.** Use tagged releases for the published downloads. [Development notes](docs/DEV.md).

**Disco! 3.3.1:** fixes Settings list-motion changes in Squash, Disco and Null, with corrected documentation and packaging. Includes **Braun 2.0**, a light and dark theme in the spirit of Dieter Rams. [Release notes](docs/releases/v3.3.1.md) · [Changelog](CHANGELOG.md)

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
> **Best-effort hobby project, experimental, as-is, no liability.** Flashing rewrites the Disc's system; an interrupted
> flash can leave it unbootable. Back up your music and keep the saved stock image. Not affiliated with FiiO, Snowsky or Ingenic.

## What's in it

Eight themes (Disco, Null, Squash, Zune, Braun 2.0, Ring, Braun, Stone), local music with playlists and a Queue, List/Wall/Coverflow and Wheel lists, and Immersive with lyrics. SongSphere, Listening Stats, Album Roulette, Boomshine and Disc Shelf are built in. See the [tour](docs/DISCO_TOUR.md) and [settings](docs/DISPLAY_SETTINGS.md).

## Install

You need a Disc on **FiiO firmware V2.57**, FiiO's V2.57 firmware ZIP and a good USB data cable.

> [!IMPORTANT]
> **Start from FiiO's stock V2.57 firmware**, not upstream diskOS or another build ([go back to stock](#going-back)).
> Already on Disco!? Just [update from the SD card](#updates-from-the-sd-card).

### Windows

Download **[diskOS-Disco-for-Windows.zip](https://github.com/zmd22/diskos-disco/releases/latest/download/diskOS-Disco-for-Windows.zip)**, extract it and run `diskOS-Disco.exe`. It handles installing, your update keys and SD updates. [Windows guide](docs/WINDOWS.md). Unsupported, but confirmed working by users. Used an older version? Click **Set up again** once.

### macOS

Follow the [macOS guide](docs/MACOS.md).

### Linux

Download the [latest release](https://github.com/zmd22/diskos-disco/releases/latest) source (or `git clone`); the interface is already in `payload/mq_ui`.

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

Power the Disc off, hold **Volume Down** and plug in USB (the screen stays black). If you made keys, check that `ota: ON … key sha256` matches `sha256sum payload/diskos-root.pub.pem` before answering `y`. Use **public**, not dev: dev blocks USB storage mode.

## Updates from the SD card

**Windows:** the app's Update page does it ([how](docs/WINDOWS.md#step-4--update-from-the-sd-card)). **Linux:** download `mq_ui` from the [latest release](https://github.com/zmd22/diskos-disco/releases/latest) and sign it with the keys installed on this Disc:

```sh
sh ui/tools/owner-keys/diskos-sign.sh path/to/mq_ui 3.3.1      # -> ./diskos-update/
```

Copy the `diskos-update` folder to the SD card root, then on the Disc: **Settings › System › Maintenance › Update from SD Card › Update**, and restart. The new version runs as a trial: choose **Keep** when asked (or let confirmed playback accept it), or **Go back**. A failing trial rolls back. More in the [SD update guide](docs/UPDATING.md).

## Build it yourself

See the [UI guide](docs/UI.md) and [build workflow](docs/DEV_WORKFLOW.md).

## Known limits

- **Firmware V2.57 only.** Start from stock, not another diskOS build.
- **Starts stock every time?** Hold **Volume Up** while powering on, then set **Settings › System › Controls › Default UI** to **diskOS**.
- **Came from another diskOS build and something's off?** **Settings › System › Maintenance › Reset diskOS Settings** (music, Wi-Fi, Bluetooth and audio stay).
- **Covers** are cached only while the SD card has 500 MB free.
- **Suddenly see FiiO's interface?** Disco! stopped; restart. `/usr/data/diskos_boot.log` (Debug Mode SSH) says why.
- **Languages:** Latin, Greek, Cyrillic, Thai, Korean, Japanese and common Chinese. No right-to-left or Indian scripts yet.
- **Updates** come by signed SD update or a fresh install; no online updates.

## Going back

- **Settings › System › Controls › Default UI › Stock** makes FiiO's interface the default; hold **Volume Up** at power-on to boot the other one once.
- `./diskos-installer restore-stock` (Linux) restores the backup made at install. The Windows app keeps it in `%LOCALAPPDATA%\diskOS-Disco\backup`; keep a copy elsewhere.
- What survives flashing, restoring and switching: [Flashing and switching](docs/SWITCHING.md).

## Documentation

| | |
|---|---|
| [Tour](docs/DISCO_TOUR.md) | Screens, controls and built-in apps |
| [How-to](docs/HOWTO.md) | Everyday tasks |
| [Updating](docs/UPDATING.md) | Keys, signing, troubleshooting |
| [Flashing and switching](docs/SWITCHING.md) | What stays when you flash or switch |
| [Windows](docs/WINDOWS.md) · [macOS](docs/MACOS.md) | Installing from a PC or Mac |
| [Changelog](CHANGELOG.md) | Every release, newest first |
| [All docs](docs/README.md) | Apps, hardware notes, development, legal |

## A note from zmd22

I’m out. Peace, and happy listening. Thank you to everyone who built, tested, shared and listened. Enjoy the music.

## Thanks

- **[namastecoconut-imagined](https://github.com/namastecoconut-imagined/diskos-disco-zune)**, for the original Zune theme: its Metro-inspired design, typography, menus and music screen. Disco's version builds on it.
- **[b0hemia](https://github.com/b0hemia/diskos)**, for diskOS itself: the reverse engineering, installer, boot and recovery chain, signed updates and the UI this grew from. [Support b0hemia on Ko-fi](https://ko-fi.com/b0hemia).
- **diskOS contributors**: everyone who reported, tested and patched upstream.
- **[ingvarr777](https://github.com/ingvarr777)**, for Ogg Opus support and careful bug reports.
- **[LVGL](https://lvgl.io)**, SQLite, jsmn, musl, the Inter, Montserrat and Font Awesome fonts, and every project in [NOTICE.md](NOTICE.md).
- **Dieter Rams** and the Braun SK4, for the design language the Braun theme borrows with respect.
- FiiO / Snowsky for a lovely little round player (no affiliation).

### A note on the contributor list

Early commits by "disco" were made by the maintainer ([zmd22](https://github.com/zmd22)) under a misconfigured email; the history is kept as is.

## License

Installer, scripts and docs: **MIT** ([LICENSE](LICENSE)). On-device interface in [ui/](ui/): **GPL-3.0-or-later** ([ui/COPYING](ui/COPYING)). Third-party terms: [NOTICE.md](NOTICE.md), [SPL_SOURCE.md](SPL_SOURCE.md), [licenses/](licenses/).

> [!WARNING]
> Never share generated `diskos_*.bin` images: they contain FiiO's root filesystem.
