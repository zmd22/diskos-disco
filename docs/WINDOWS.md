# Flashing from Windows with WSL

> **Highly experimental and unsupported.** Nobody has tested this guide, including the maintainer, and it will not be
> supported: if it doesn't work for you, please flash from Linux. diskOS Disco! is only flashed and tested from Linux.
> If you try it anyway and it works (or doesn't), a note in the repo's issues helps the next person.

The installer is a Linux program. WSL runs Linux inside Windows, and the `usbipd` tool hands the Disc's USB connection
to it. Each step is standard, but this combination has not been tried.

You need Windows 11 (or Windows 10 22H2), the official V2.57 firmware ZIP and a good USB cable.

## 1. Install WSL and Ubuntu

In PowerShell as administrator, then restart:

```powershell
wsl --install -d Ubuntu
```

## 2. Install the USB bridge for WSL

In PowerShell as administrator:

```powershell
winget install usbipd
```

## 3. Set up the installer in Ubuntu

Open "Ubuntu" from the Start menu:

```sh
sudo apt update && sudo apt install -y git python3 python3-venv python3-tk squashfs-tools usbutils
git clone https://github.com/zmd22/diskos-disco
cd diskos-disco
./install.sh
sudo cp udev/70-diskos-maskrom.rules /etc/udev/rules.d/ && sudo udevadm control --reload
cp /mnt/c/Users/YOUR_WINDOWS_NAME/Downloads/SNOWSKY_DISC_update_20260909_v257.zip .
```

Replace `YOUR_WINDOWS_NAME` with your Windows user name.

## 4. Put the Disc in flashing mode

Power it off, hold **Volume Down** and plug in USB.

## 5. Hand the Disc to WSL

In PowerShell as administrator:

```powershell
usbipd list
```

Find the line with **a108:eaef** and note its BUSID (for example `2-3`), then:

```powershell
usbipd bind --busid 2-3
usbipd attach --wsl --busid 2-3 --auto-attach
```

Leave that window open: `--auto-attach` reconnects the Disc if it drops off USB during the flash.

## 6. Flash

In Ubuntu, check that `lsusb` lists `a108:eaef`, then:

```sh
./diskos-installer gui
```

Pick the firmware ZIP and `payload/mq_ui`, then Install. Or use the command line from the
[release notes](https://github.com/zmd22/diskos-disco/releases/latest).

## If it fails

Nothing is lost: the Disc's flashing mode is in its ROM. Unplug, put the Disc back in flashing mode (step 4) and repeat
steps 5 and 6, or flash from a Linux computer instead.
