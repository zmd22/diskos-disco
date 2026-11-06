# Security policy

diskOS is an unsupported beta that flashes firmware through the SoC's mask-ROM USB mode. Defects can brick devices
or write to the wrong hardware, so security and safety reports are prioritised.

## Reporting

**Don't open a public issue.** Use GitHub private vulnerability reporting (**Security › Report a vulnerability**).

Include the issue and reproduction steps, impact (e.g. out-of-partition writes, wrong-device targeting, host
privilege escalation, data exposure), host OS, installer version and device firmware. Redact serials, MAC addresses
and network credentials.

## Especially in scope

- **Wrong-device / wrong-partition writes:** anything letting the flasher write a device it should refuse, or
  outside the rootfs region.
- **Host privilege:** the installer runs as a normal user (udev rule, no `sudo`). Path traversal, symlink following,
  arbitrary delete, predictable temp files.
- **Untrusted input:** crafted firmware or squashfs that escapes the work directory or executes code.
- **Bricking:** any sequence that leaves a device unrecoverable.

## Debug Mode

**Settings › System › Debug Mode** (off by default) starts SSH over Wi-Fi with **root access**, using a random
password generated per enable in a private shadow file bind-mounted over `/etc/shadow`. The stock password is never
used or exposed.

- The current password is also stored **in plain text** at `/usr/data/sshd/current_pw` (0600, root) so the UI can
  show it after restart; disabling deletes it. A reboot while enabled can leave a stale copy, but the overlay is
  dropped on reboot so it no longer authenticates.
- SSH is Dropbear **2022.83**, which lacks the CVE-2023-48795 (Terrapin) strict-KEX fix (update planned). Exposure is
  limited by Debug Mode being opt-in and short-lived.
- **dev** builds have a passwordless root shell on USB serial (physical access only); **public** builds have none.
  Report only if it's reachable without physical access.

In scope: leaking the debug password, SSH or the overlay surviving disable, the overlay corrupting or leaking on-disk
credentials, or network access to the stock `/etc/shadow`.

## Response

Hobby project, no SLA. Expect acknowledgement within about a week; confirmed device-safety fixes are called out in
release notes.

## Supported versions

Latest release only; no backports.
