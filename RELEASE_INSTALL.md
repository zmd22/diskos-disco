# Install diskOS Disco!

> Hobby project, as-is, no warranty. Not affiliated with FiiO or Snowsky. Back up your music and keep the stock image.

- **Base:** install over stock FiiO V2.57. Upstream diskOS or other builds are unsupported; restore stock first.
- **Coming from another diskOS build:** if anything misbehaves, run **Settings › System › Maintenance › Reset diskOS Settings** (keeps music, Wi-Fi, Bluetooth, EQ, audio). Restoring stock does not clear diskOS data.
- **Boots stock after install:** a previous "Default UI: Stock" survives flashing. Hold **Volume Up** at power-on, then set **Settings › System › Controls › Default UI** to **diskOS**. See [flashing and switching](https://github.com/zmd22/diskos-disco/blob/v3.6.1/docs/SWITCHING.md).

## Install

- **Windows:** extract `diskOS-Disco-for-Windows.zip`, run `diskOS-Disco.exe` (experimental). [Guide](https://github.com/zmd22/diskos-disco/blob/v3.6.1/docs/WINDOWS.md)
- **macOS:** [Mac guide](https://github.com/zmd22/diskos-disco/blob/v3.6.1/docs/MACOS.md), with links to upstream's Apple Silicon flash tools.
- **Linux:** install the Fedora or Arch package (experimental, [guide](https://github.com/zmd22/diskos-disco/blob/v3.6.1/docs/LINUX.md)), or use the CLI installer from source ([README](https://github.com/zmd22/diskos-disco/blob/v3.6.1/README.md#install)).

On 1.4.1 or later, 3.6.1 installs by SD update. Older versions need one reflash (the 1.4.1 boot-script fixes can't ship via SD).

## Updates

`mq_ui` is the interface. Sign it with your keys and install from the SD card: [UPDATING.md](https://github.com/zmd22/diskos-disco/blob/v3.6.1/docs/UPDATING.md).

## Notable

- **3.6.1:** Graphite, a monochrome LCD-style list theme in the ChicagoFLF font, with a White, Grey or Amber backlight (PR #31 by @ericklv).
- **3.6.0:** progressive lyrics in Lyrics and Immersive (word highlights, tap a line to seek; PR #29 by @Grippy98); Cat Dash, a built-in game (PR #26 by @ericklv); Zune Outdoor fix (PR #27 by @ericklv); Disc Shelf SMB on older FAT/exFAT cards (PR #28 by @Grippy98).
- **3.5.1:** Lock on timeout (Settings › Battery & power, Off by default): a screen that turns off by itself locks like the power button, so pocket touches can't wake it and one press does.
- **3.5.0:** interface polish: nothing-playing shortcuts in every theme, Squash and Zune easier to read, Shade and layout fixes. Screen Off now acts with Screensaver Off.
- **3.4.4:** battery life back to 3.4.1: large fonts stored uncompressed again.
- **3.4.3:** the Shade Quick Settings redesigned for the round screen; Disc Shelf fixes for Zune (PR #25 by @ericklv).
- **3.4.2:** Cyrillic in Zune (PR #24 by @meekieD); smaller interface (undone in 3.4.4); battery totals restart after a full charge; Squash follows the Accent colour; Disc Shelf A-Z keypad.
- **3.4.1:** MA Sendspin 1.0 (experimental): encrypted, code pairing, album art, FLAC. Needs Music Assistant 2.10.7+.
- **3.4.0:** aptX / aptX HD (experimental): **Settings › Connectivity › aptX (experimental)**. [Bluetooth guide](https://github.com/zmd22/diskos-disco/blob/v3.6.1/docs/BLUETOOTH.md)
- Built in: [Disc Shelf](https://github.com/zmd22/diskos-disco/blob/v3.6.1/docs/DISC_SHELF_INSTALL.md) (Navidrome / SMB3 downloads), Boomshine, Cat Dash, [Album Roulette](https://github.com/zmd22/diskos-disco/blob/v3.6.1/docs/ALBUM_ROULETTE_INSTALL.md). Add them via the theme's Menu items editor. The standalone Disc Shelf 1.0.4 is no longer packaged (last in 3.3.1).

This package is not a flasher and contains no FiiO firmware, music, keys or account data.
