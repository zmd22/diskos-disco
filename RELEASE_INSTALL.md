# Install diskOS Disco!

> **Best-effort hobby project, experimental, as-is.** No warranty or liability; not affiliated with FiiO or Snowsky.
> Back up your music and keep the saved stock image before you install.

> **Recommended: start from FiiO's stock V2.57 firmware.** Installing over upstream diskOS or another diskOS build is not supported and can be buggy; restore stock first.

> Switched to Disco from upstream diskOS or another build, and something looks or behaves wrong? **Settings > System > Maintenance > Reset diskOS Settings** gives the interface a fresh start; your music, Wi-Fi, Bluetooth, EQ and audio settings stay. Going back to stock in between does *not* clear what diskOS leaves on the Disc.
>
> **Starts stock after installing?** An earlier "Default UI: Stock" choice survives flashing. Hold **Volume Up** while
> powering on to start Disco once, then set Settings > System > Controls > Default UI to **diskOS**.
> More: [what stays and what goes when you flash or switch](docs/SWITCHING.md).

**On Windows:** download `diskOS-Disco-for-Windows.zip` from the release, extract it and run `diskOS-Disco.exe`. It
checks your PC, flashes the Disc and makes SD updates ([guide](docs/WINDOWS.md)).

**On macOS:** follow the [Mac guide](docs/MACOS.md), including links to upstream’s ready-built Apple Silicon flash tools.

**On Linux:** flash with the installer from the repository or source download, using your update key
([README](README.md#install)). **Already on 1.4.1 or later? 3.2.2 supports an SD update.** Older than that? Reflash once first
(the 1.4.1 boot-script fixes can't arrive by SD card). See [what's new](https://github.com/zmd22/diskos-disco/releases/latest).

After that, `mq_ui` is the interface: sign it with your own keys and install it from the SD card
([docs/UPDATING.md](docs/UPDATING.md)).

Optional apps, installed separately:

- [Disc Shelf](docs/DISC_SHELF_INSTALL.md): Navidrome browser and original-file downloader.

Boomshine and [Album Roulette](docs/ALBUM_ROULETTE_INSTALL.md) are built into `mq_ui`: add them in the theme’s Menu items editor. Installed apps can join that menu too.

This package is not a flasher. It contains no FiiO firmware, music, signing keys or account details.

## Try Squash

Choose Display → Theme → Squash for big bold Music text, touch playback and the D! launcher. Keep Null or your existing theme if you prefer; their saved settings remain. Settings → Help, below Battery, contains How to and first-use hints. See [the 2.5.0 notes](docs/releases/v2.5.0.md).
