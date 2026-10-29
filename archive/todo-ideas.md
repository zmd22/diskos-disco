> Historical proposals, not shipped 3.2.1 features. See [current targets](../todo.md).

# diskOS ideas for later

Updated: 2026-10-12. Active development tasks are in `todo.md`.

## Windows point-and-click installer — shelved

Owner explicitly shelved this on 2026-10-12. Planning only; no Windows installer
implementation, build or device test has been performed. Resume when requested.

### Goal

One approachable Windows setup EXE with a wizard that lets users choose:

- Their official Snowsky firmware ZIP.
- The bundled stable mq_ui, or an optional custom mq_ui binary.
- The official update root public key, their own P-256 root public PEM, or
  signed updates disabled. Private signing keys never go into the firmware.

Bundle Python, GUI dependencies, payload and prebuilt tools wherever possible.
Clearly explain missing system prerequisites and offer guided installation;
users should not need to compile tools or use terminal commands in the finished
product. Include validation, clear progress/errors, stock backup and restoration.
Optional later owner tools: key generation and signed SD-update packaging.

### Proposed approach

Start with a native Windows wizard and the existing Linux image builder/flasher
inside an app-owned WSL2 distribution. Bundle a preconfigured backend with
Python dependencies, LZO-capable squashfs tools, libusb, usbboot and the exact
existing NAND writer/SPL. Use the Linux filesystem for extraction and repacking,
not `/mnt/c`: the stock rootfs includes case-colliding filenames and depends on
Linux permissions/symlinks. Preserve independent output validation, writer
identity/capacity checks, device-side firmware-base gate and fail-closed results.

Windows 11 x64 first. WSL2 and usbipd-win remain system prerequisites; guided
setup can require internet, administrator approval, virtualization and a restart.
After provisioning, local build/flash should work offline. Never modify the
user's other WSL distributions or redistribute FiiO firmware.

If USB/IP is unreliable, investigate native Windows USB with WinUSB plus a
ported usbboot, retaining WSL for image building. Fully native image building is
a larger separate task requiring Linux metadata preservation independent of
ordinary Windows filesystem extraction.

### First checkpoint when resumed: non-destructive USB test

The owner has a Windows 11 PC nearby. Instructions were provided, but **no
results were received and no Windows compatibility has been established**.

1. In administrator PowerShell, install Ubuntu for the feasibility test:
   `wsl --install -d Ubuntu`. Restart if requested and finish Ubuntu account setup.
2. Install the bridge:
   `winget install --interactive --exact dorssel.usbipd-win`.
   Reopen Terminal; record `wsl --list --verbose` and `usbipd --version`.
   Ubuntu must be WSL2; use `wsl --set-version Ubuntu 2` if necessary.
3. In Ubuntu: `sudo apt update && sudo apt install -y usbutils python3-usb`.
   Leave Ubuntu open to keep WSL running.
4. Power off the Disc, hold Volume Down and connect a USB data cable directly.
   The screen stays black. Run `usbipd list` in PowerShell and identify exactly
   one `a108:eaef` device. Use its actual BUSID in
   `usbipd bind --busid BUSID` (administrator) and
   `usbipd attach --wsl --busid BUSID`.
5. In Ubuntu: `lsusb -d a108:eaef`, then run the read-only probe below.
6. Record output; detach with `usbipd detach --busid BUSID`, unplug and boot
   normally. Substitute the actual BUSID; do not paste the placeholder literally.

The probe matches `src/usbboot/usbboot.c`'s GET_CPU_INFO request. It only claims
interface 0 and reads eight bytes; it uploads no code and performs no NAND write.
Using sudo here is only for the USB probe, not the actual diskOS installer.

```sh
sudo python3 - <<'PY'
import sys
import usb.core
import usb.util

devices = list(usb.core.find(
    find_all=True, idVendor=0xa108, idProduct=0xeaef
))
if len(devices) != 1:
    sys.exit(f"FAIL: expected exactly one Disc, found {len(devices)}")

dev = devices[0]
claimed = False
try:
    usb.util.claim_interface(dev, 0)
    claimed = True
    print("PASS: opened the Disc and claimed its USB interface")
    reply = bytes(dev.ctrl_transfer(0xC0, 0, 0, 0, 8, timeout=3000))
    if len(reply) != 8:
        sys.exit(f"FAIL: expected 8 CPU-info bytes, received {len(reply)}")
    print("PASS: boot ROM answered the CPU-info request")
    print("CPU info:", repr(reply))
    print("Raw bytes:", reply.hex())
except usb.core.USBError as exc:
    sys.exit(f"FAIL: USB communication: {exc}")
finally:
    if claimed:
        try:
            usb.util.release_interface(dev, 0)
        except usb.core.USBError:
            pass
    usb.util.dispose_resources(dev)
PY
```

Two PASS messages establish basic enumeration, interface access and a real
boot-ROM transaction. They do not establish sustained-transfer or flashing
reliability. Follow with a deliberately non-destructive transfer qualification,
then image preparation, and finally an explicitly approved physical flash/boot/
restore test. Do not automatically retry a flash with an uncertain result.

### Scope and next steps

- [ ] Owner resumes the idea and runs the USB feasibility test.
- [ ] Qualify larger transfers/reconnects; choose USB/IP or native USB backend.
- [ ] Build a wizard prototype with pickers, prerequisite checks and Prepare
      image only; keep mq_ui unchanged.
- [ ] Add confirmed install/restore and test on Windows with a real Disc.
- [ ] Package dependencies/licenses/corresponding source, update website/docs
      and qualify a public release.

A testable prototype is a few focused development sessions if USB works; this
is not a guaranteed elapsed-time estimate. The earlier 3–4 engineering-week
estimate included broader public-release polish, owner tools and qualification.

Platform references:

- https://learn.microsoft.com/en-us/windows/wsl/connect-usb
- https://learn.microsoft.com/en-us/windows/wsl/install
- https://learn.microsoft.com/en-us/windows/wsl/basic-commands
- https://github.com/libusb/libusb/wiki/Windows
- https://pyinstaller.org/en/latest/
