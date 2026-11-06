# diskOS Disco! for Windows

[README](../README.md) · [Tour](DISCO_TOUR.md) · [SD updates](UPDATING.md)

> Hobby project, as-is, no warranty. Not affiliated with FiiO or Snowsky. Back up your music and keep the stock image.

> - **Upgrading from an older app:** click **Set up again** once on the Setup tab, or it keeps installing the version it was first set up with.
> - **Support:** unsupported but user-confirmed (installs and SD updates). No help for Windows-specific issues.
> - **Base:** install over stock V2.57, not another diskOS build.
> - **Auto-update:** on start the app fetches newer installer files from GitHub (checksum-verified). Install and Update download the latest `mq_ui` unless you pick a file. Offline, it uses what it has.
> - **After another diskOS build:** **Settings › System › Maintenance › Reset diskOS Settings** if anything misbehaves. If it boots stock, see [Switching](SWITCHING.md).

A GUI for installing Disco! and making signed SD updates, with five independent tabs: Check, Setup, Install, Update,
Restore. The tabbed layout (3.4.0) is experimental; screenshots show the earlier layout with the same controls.

<p align="center">
  <img src="windows/check.png" width="400" alt="Check: the PC check, all green">
  <img src="windows/install.png" width="400" alt="Install: installing on the Disc">
</p>

**[Download diskOS-Disco-for-Windows.zip](https://github.com/zmd22/diskos-disco/releases/latest/download/diskOS-Disco-for-Windows.zip)**

## How it works

The installer is a Linux program that needs low-level USB access. The app runs it in a private ~150 MB WSL distro and
passes the Disc through with usbipd-win. It runs the same installer and safety checks as Linux, and leaves any other
WSL distros alone.

## Requirements

| | |
|---|---|
| PC | Windows 11 x64 (not ARM); Windows 10 22H2 untested. ~10 GB free. |
| Virtualization | Enabled (the app checks). |
| Internet | Once, for WSL and usbipd-win. Flashing works offline. |
| Disc | Firmware V2.57, charged, USB data cable direct to the PC (no hub). |
| Firmware | `SNOWSKY_DISC_update_20260909_v257.zip` from FiiO. |
| Time | ~30 minutes. |

## Start

1. Extract the ZIP. Keep `diskOS-Disco.exe` and `diskos-wsl.tar.gz` together.
2. Run `diskOS-Disco.exe`. It's not code-signed: on SmartScreen click **More info › Run anyway**.
3. Read **Good to know** and click **I understand, continue**.

<p align="center"><img src="windows/disclaimer.png" width="420" alt="Good to know"></p>

The app runs unelevated; Windows prompts only to install WSL and to share the Disc's USB the first time.

## Check

| Check | If missing |
|---|---|
| Windows 11 x64 | Shows what's supported. |
| Virtualization | **How to turn it on** links Microsoft's BIOS/UEFI guide. |
| WSL | **Install WSL**, then restart Windows. |
| usbipd-win | **Install USB bridge** runs its installer. |
| Disk space | Free up to ~10 GB. |
| Pending restart | **Restart now**. |

Fix from the top down, then **Check again**. **Copy report** copies results for bug reports.

## Setup

<p align="center"><img src="windows/prepare.png" width="480" alt="Setup: diskOS's own Linux environment, ready"></p>

**Set up** imports the **diskOS-Disco** distro (minimal Ubuntu with the installer and latest `mq_ui`) from
`diskos-wsl.tar.gz` and runs a self-test (1–2 minutes). It runs only while the app needs it.

- **Test it**: rerun the self-test. **Remove it**: delete the distro.
- **Needs updating** after an app update: **Set up again**.

The stock backup is always stored in a Windows folder, so removing the distro never loses it.

## Install

<p align="center"><img src="windows/install.png" width="480" alt="Install: firmware, version, keys, Disc connected"></p>

1. **Firmware:** choose the V2.57 ZIP.
2. **Version:** **The latest release** (downloaded and checksum-verified) or a local `mq_ui`. Offline, the bundled version is offered.
3. **Update keys:** first install, **Create new update keys** (saved to `Documents\diskOS Keys`). Installed before: **Use my existing keys folder**; a Disc only accepts updates signed with its install keys.
4. **Connect:** Disc off, hold **Volume Down**, plug in (screen stays black), **Find my Disc**. Approve USB sharing the first time.
5. **Install** and confirm. It backs up the original system and flashes (~20 min). Don't unplug; the app keeps the PC awake.
6. At **Done**, unplug and power on. Disco! finishes installing on first boot.

> [!IMPORTANT]
> Back up `Documents\diskOS Keys` somewhere private. Without it you must reflash to update; anyone with `leaf.key` can update your Disc.

## Update from the SD card

<p align="center"><img src="windows/update.png" width="480" alt="Update: a signed update for the SD card"></p>

Works even on a PC that didn't do the install, as long as you have the Disc's keys.

1. **Version:** latest release (checksum-verified) or a local `mq_ui`.
2. **Keys:** the install keys (auto-detected in `Documents\diskOS Keys`).
3. **SD card:** the card in a reader, or the Disc in USB storage mode. Or **Save to a folder instead**.
4. **Make the update** signs it and writes the six `diskos-update` files individually.
5. On the Disc: **Settings › System › Maintenance › Update from SD Card › Update**, restart, play music for a few minutes to accept. Failures roll back automatically.

Details: [UPDATING.md](UPDATING.md).

## Restore

Flashes the install-time stock backup back (Disc in flash mode). Music stays. To just boot FiiO's UI:
**Settings › System › Controls › Default UI › Stock**.

## Footprint

- **WSL** and **usbipd-win**: installed by you; uninstall via Settings › Apps.
- **Distro:** `%LOCALAPPDATA%\diskOS-Disco\wsl`; remove in Setup.
- **App data:** `%LOCALAPPDATA%\diskOS-Disco` (`backup`, last `update`, a helper script). Keep `backup` if you may restore stock.
- **Keys:** `Documents\diskOS Keys`, if the app made them.

Nothing else is installed system-wide, and nothing runs at startup.

## Privacy

Network access only for WSL/usbipd-win installs and GitHub release downloads. Firmware, keys and music stay local.
See [Privacy](PRIVACY.md).

## Troubleshooting

| Symptom | Fix |
|---|---|
| "Windows protected your PC" | **More info › Run anyway**. |
| Install WSL does nothing | Accept the UAC prompt, restart Windows, reopen. |
| Virtualization stays red | Enable VT-x / AMD-V (SVM) in BIOS/UEFI. |
| Setup: **Package missing** | Extract the whole ZIP; keep `diskos-wsl.tar.gz` next to the exe. |
| **No Disc found** | Off, Volume Down, plug in. Data cable, no hub. |
| **Didn't reach diskOS** | Unplug, power off, Volume Down, replug, **Find my Disc**. The log shows usbipd's message. |
| Install stops | Nothing is written until the final step; retry from Volume Down. Otherwise **Copy report** and open an [issue](https://github.com/zmd22/diskos-disco/issues). |
| SD update refused | Use the install keys and all six files. See [UPDATING.md](UPDATING.md#troubleshooting). |

Back to stock: [Going back](../README.md#going-back).

## Manual setup

Install WSL (Ubuntu) and usbipd-win, attach with `usbipd bind` and
`usbipd attach --wsl --busid <BUSID> --auto-attach`, then follow the [Linux instructions](../README.md#install) inside
Ubuntu. Add a udev rule with `GROUP="plugdev"` (WSL has no login seat). The app's distro is built by
[`windows/wsl/build-env.sh`](../windows/wsl/build-env.sh); app source is in [`windows/`](../windows/).
