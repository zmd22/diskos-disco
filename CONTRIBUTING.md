# Contributing

diskOS can brick real hardware. These rules keep contributions safe and legally clean.

## Rules

1. **Test hardware-affecting changes on a real Disc.** Image builder, boot hook, NAND writer, SPL and device
   targeting changes must be flashed and booted before they're called working. State firmware version and DRAM/NAND
   variant.
2. **Stay fail-closed.** The first-boot hook falls back to stock when it can't verify the UI against the baked
   manifest; the flasher aborts rather than write unverified data or to the wrong device.
3. **Every new failure gets an error code** (`E1xx` preflight, `E2xx` build, `E3xx` flash, `F1xx` device writer),
   documented in `README.md`.
4. **Be honest about limits.** Don't imply something works until it's verified.

## Licensing

- `ui/` is **GPL-3.0-or-later**; everything else is **MIT**. Contributions take the license of the files they touch.
  Sign off commits (`git commit -s`, DCO).
- Never upload FiiO firmware, stock rootfs or generated `diskos_*.bin` images. Building locally is fine;
  redistributing isn't.
- When changing how GPL/LGPL components are bundled (usbboot, squashfs-tools, SPL, libusb), keep corresponding
  source and `NOTICE.md` in sync.
- No device identifiers (serials, MACs) or secrets in commits.

## Issues

Include firmware version and hash, DRAM/NAND variant if known, host OS, the error code, the bad-block count if
printed, and redacted logs. Don't post serials, MACs, firmware, rootfs or images.

## Development

- Project brief (hardware facts, layout, safety rules): [`agents/AGENTS.md`](agents/AGENTS.md).
- Installer build: `bash build/build.sh`.
- UI: [`docs/DEV_WORKFLOW.md`](docs/DEV_WORKFLOW.md) (builds, host previews, SD updates) and
  [`docs/UPDATING.md`](docs/UPDATING.md) (keys, trials, rollback). [`tools/`](tools/) has screenshot and touch
  diagnostics helpers. UI deployment always goes through signed SD updates.
