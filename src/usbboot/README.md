# usbboot (Ingenic X2000 mask-ROM USB loader)

This is the host-side loader that talks to the Ingenic X2000's on-chip mask-ROM USB boot
mode (`a108:eaef`). diskOS's installer uses it to upload the stage-1 SPL and the on-device
NAND writer during a flash.

## Provenance and license

- **License:** GPL-2.0-or-later.
- **Copyright:** (c) 2021 Aidan MacDonald, (c) 2015 Amaury Pouly, and contributors.
- Part of the Ingenic X2000 community mask-ROM tooling lineage (the same tool family is used
  to recover other X2000 boards). The copy here is included as GPL corresponding source; see
  the repository's [`NOTICE.md`](../../NOTICE.md).

## Local modifications for diskOS

- Larger USB transfer chunks and a longer command timeout, so the ~90-minute on-device NAND
  writer stage does not time out.
- Built fully statically for portability - see [`build/build-usbboot-static.sh`](../../build/build-usbboot-static.sh).

## Building

```
make                     # dynamic build (needs libusb-1.0 dev headers)
```

For the portable static binary the installer ships, use
[`build/build-usbboot-static.sh`](../../build/build-usbboot-static.sh) instead, which pins the
libusb source and links it statically.

## Note

This tool drives raw mask-ROM USB and can erase/write device storage. It is intended to be
invoked by the diskOS installer, which adds device checks and the fail-closed flashing flow.
Running it directly is for developers who understand the Ingenic boot protocol.
