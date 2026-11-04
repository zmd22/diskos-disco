# diskOS Disco! for Linux (experimental)

[Back to the README](../README.md) · [Windows](WINDOWS.md) · [SD updates](UPDATING.md)

> **Best-effort hobby project, experimental, as-is.** No warranty or liability; not affiliated with FiiO or Snowsky.
> Back up your music and keep the saved stock image before you install. The Linux app is new in 3.4.0.

The Linux app has the Windows app's tabs, each usable on its own at any time:

| Tab | What it does |
|---|---|
| **Check** | What this PC needs (USB access for flashing, tools, installer files), with one-click fixes. |
| **Install** | FiiO's V2.57 firmware ZIP + the interface + your update keys (new or existing), then flashes the Disc. |
| **Update** | An `mq_ui` + your keys → a signed `diskos-update` folder on the Disc's SD card, or a folder to copy by hand. |
| **Restore** | Back to FiiO's stock firmware, from the backup made at install. |

Start from FiiO's stock **V2.57** firmware, not another diskOS build.

## Packages (Fedora, Arch)

Download from the [latest release](https://github.com/zmd22/diskos-disco/releases/latest) and install:

```sh
sudo dnf install ./diskos-disco-*.fc*.x86_64.rpm           # Fedora
sudo pacman -U diskos-disco-*-x86_64.pkg.tar.zst           # Arch
```

Then open **diskOS Disco!** from your applications menu (or run `diskos-disco`). The package includes the interface,
the static flash tools and the udev rule that lets your user reach the Disc in flash mode. Unplug and replug the Disc
once after installing the package.

## From the source

Download the [latest release](https://github.com/zmd22/diskos-disco/releases/latest) source (or `git clone`), then:

```sh
./install.sh                     # first time only: host setup (tools, USB access)
./diskos-installer               # opens the app
./diskos-installer desktop-entry # optional: add it to your applications menu
```

The command-line installer is still there (`./diskos-installer install …`, `restore-stock`, `doctor`); see the
[README](../README.md#install).

## Flashing

Power the Disc off, hold **Volume Down** and plug in USB: the screen stays black (flash mode). The app installs the
**public** variant (USB storage mode stays available). It takes about 20 minutes: keep the Disc connected and the PC
awake until it says Done.
