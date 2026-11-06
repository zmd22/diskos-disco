# diskOS Disco! for Linux (experimental)

[Back to the README](../README.md) · [Windows](WINDOWS.md) · [SD updates](UPDATING.md)

> Hobby project, as-is, no warranty. Not affiliated with FiiO or Snowsky. Back up your music and keep the stock image.

Same tabs as the Windows app, each independent:

| Tab | What it does |
|---|---|
| **Check** | Host prerequisites (USB access, tools, installer files) with one-click fixes. |
| **Install** | Flashes the Disc from the V2.57 ZIP, an `mq_ui` and your update keys (new or existing). |
| **Update** | Signs an `mq_ui` into a `diskos-update` folder on the SD card or a local folder. |
| **Restore** | Restores stock firmware from the install-time backup. |

Install over stock **V2.57**, not another diskOS build.

## Packages (Fedora, Arch)

From the [latest release](https://github.com/zmd22/diskos-disco/releases/latest):

```sh
sudo dnf install ./diskos-disco-*.fc*.x86_64.rpm           # Fedora
sudo pacman -U diskos-disco-*-x86_64.pkg.tar.zst           # Arch
```

Launch **diskOS Disco!** or `diskos-disco`. The package ships `mq_ui`, static flash tools and the udev rule for
flash-mode access; replug the Disc once after installing.

## From the source

From the release source or a clone:

```sh
./install.sh                     # first time only: host setup (tools, USB access)
./diskos-installer               # opens the app
./diskos-installer desktop-entry # optional: add it to your applications menu
```

CLI subcommands (`install`, `restore-stock`, `doctor`): see the [README](../README.md#install).

## Flashing

Disc off, hold **Volume Down**, plug in USB (screen stays black). The app flashes the `public` variant (USB storage
stays available). Takes ~20 minutes; keep the Disc connected and the PC awake until **Done**.
