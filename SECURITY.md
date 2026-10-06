# Security policy

diskOS is an **unsupported beta** that flashes firmware over a device's mask-ROM USB mode. Because
a defect here can brick a device or write to the wrong hardware, security and safety reports are
taken seriously.

## Reporting a vulnerability

**Do not open a public issue for a security or device-safety vulnerability.** Instead, use GitHub's
private vulnerability reporting for this repository (the "Report a vulnerability" button under the
**Security** tab), which opens a private advisory visible only to the maintainer.

Please include, where relevant:

- What the issue is and how to reproduce it.
- The impact (e.g. writes outside the intended partition, wrong-device targeting, host privilege
  issues, data exposure).
- Your host OS, the installer version, and the device firmware version.

Please redact anything device-identifying from logs before sending: serial numbers, Bluetooth/Wi-Fi
MAC addresses, and network credentials.

## Especially in scope

- **Wrong-device / wrong-partition writes** - anything that could let the flasher erase or write a
  device it should have refused, or write outside the intended rootfs region.
- **Host privilege issues** - the installer is designed to run as your **normal (non-root) user**
  (USB access via the bundled udev rule); it should not need `sudo`. Path-traversal, symlink-follow,
  arbitrary-delete, or predictable-temp-file issues matter.
- **Unauthenticated input** - the installer extracts a squashfs and decrypts a firmware package;
  report anything that lets crafted input escape the work directory or run code.
- **Bricking-class bugs** - a report that a specific sequence leaves a device unrecoverable.

## Debug Mode (on-device remote access)

diskOS has an opt-in **Debug Mode** (Settings > System > Debug Mode), **off by default**, that starts
an SSH server over WiFi. It uses a **random password generated per enable**, placed into a private
shadow file bind-mounted over `/etc/shadow`; the device's stock password is never used or exposed.
While it is on, it grants **root access over the network**.

- **Password storage:** while Debug Mode is on, the current password is also stored in **plaintext** at
  `/usr/data/sshd/current_pw` (mode 0600, root-only) so the UI can redisplay it after a restart;
  disabling Debug Mode deletes it. A reboot while still enabled can leave a stale copy - the SSH
  overlay is dropped on reboot so that password no longer authenticates until re-enabled. It is only
  as protected as root/physical access to the device.
- **SSH server:** Dropbear **2022.83**, which predates the CVE-2023-48795 "Terrapin" Strict-KEX
  mitigation (update planned); exposure is limited by Debug Mode being opt-in and short-lived.
- **Serial:** the local USB-serial root shell exists only on **dev** builds and is passwordless-root by
  design (physical-USB access only); **public builds have no serial shell**. Report only if it is
  reachable without physical access.

In scope: anything that exposes the debug password, leaves SSH running (or the shadow overlay mounted)
after it should be off, lets the overlay corrupt/leak the on-disk credentials, or reaches the stock
`/etc/shadow` over the network.

## Response

This is a hobbyist project with no SLA, but security and bricking reports are prioritised over
features. Expect an initial acknowledgement within a week or so. Fixes for confirmed
device-safety issues will be called out clearly in the release notes.

## Supported versions

Only the latest release is supported. There are no backported fixes for older builds.
