# Install diskOS Disco! from macOS

> Hobby project, as-is, no warranty. Not affiliated with FiiO or Snowsky. Back up your music and keep the stock image.

> - Install over stock V2.57; upstream diskOS and other builds are unsupported.
> - After another diskOS build, run **Settings › System › Maintenance › Reset diskOS Settings** if anything misbehaves.
> - Boots stock after install: hold **Volume Up** at power-on, then set **Settings › System › Controls › Default UI** to **diskOS**. See [Switching](SWITCHING.md).

Requires a Mac, a USB data cable, an unmodified **FiiO V2.57 firmware ZIP** and ~30 minutes. Based on
[b0hemia's diskOS 1.2.0 install notes](https://github.com/b0hemia/diskos/tree/v1.2.0#install). Run everything as
your normal user, **without sudo**. Not yet tested on a physical Mac.

## 1. Source and Python

Unpack the source tarball from the [latest release](https://github.com/zmd22/diskos-disco/releases/latest):

```sh
cd ~/Downloads/diskos-disco-*
python3 --version   # needs 3.8+
uname -m            # arm64 or x86_64
```

Get Python from [python.org](https://www.python.org/downloads/macos/) (includes Tk for the GUI). With Homebrew,
`python3` plus the `python-tk@<major.minor>` that `./install.sh` reports; the CLI doesn't need Tk. On Apple Silicon
use a native (non-Rosetta) shell.

## 2. Flash tools

### Apple Silicon: prebuilt

From [b0hemia/diskOS 1.2.0](https://github.com/b0hemia/diskos/releases/tag/v1.2.0), download
[diskos-installer-macos-arm64.tar.gz](https://github.com/b0hemia/diskos/releases/download/v1.2.0/diskos-installer-macos-arm64.tar.gz)
and [SHA256SUMS](https://github.com/b0hemia/diskos/releases/download/v1.2.0/SHA256SUMS) to `~/Downloads`, verify,
then extract only the native tools into the Disco source folder:

```sh
(cd ~/Downloads && shasum -a 256 diskos-installer-macos-arm64.tar.gz)   # must match SHA256SUMS
mkdir -p vendor
tar -xzf ~/Downloads/diskos-installer-macos-arm64.tar.gz \
  -C vendor --strip-components 2 diskos-installer/vendor/macos-arm64
```

This provides `usbboot`, `mksquashfs`, `unsquashfs`, their libs and loader files in `vendor/macos-arm64/` (identical
to Disco's). Keep using Disco's installer and payload.

### Intel: build once

```sh
xcode-select --install     # if missing; Homebrew is also required
bash vendor/setup-macos.sh # builds vendor/macos-x86_64/
```

The script pins dependency versions ([build inputs](../build/NATIVE_TOOLS.md#pinned-macos-arm64-build-inputs)). On
a version mismatch, follow its instructions; don't bypass the check. Upstream has no prebuilt Intel package.

## 3. Prepare

```sh
./install.sh                # local Python env
./diskos-installer doctor   # "player missing" is expected until it's in flash mode
sh ui/tools/owner-keys/diskos-keys.sh          # optional, once: update keys
cp diskos-keys/diskos-root.pub.pem payload/
```

Keep `diskos-keys` private and backed up; reuse existing keys if you have them. Without keys, omit `--ota-key` below.

## 4. Flash

Keep the Mac awake. Disc off, hold **Volume Down**, connect USB (screen stays black); allow the USB accessory if asked.

```sh
./diskos-installer install \
  --firmware ~/Downloads/SNOWSKY_DISC_update_20260909_v257.zip \
  --variant public \
  --ui payload/mq_ui \
  --ota-key payload/diskos-root.pub.pem
shasum -a 256 payload/diskos-root.pub.pem      # compare with the summary's key fingerprint before confirming
```

Wait for flash and verification to complete. `public` keeps USB storage; SSH is available via Debug Mode.
GUI alternative: `./diskos-installer gui` (Public, firmware ZIP, `payload/mq_ui` as custom UI, your public key).

## Updates and recovery

- Updates: [signed SD updates](UPDATING.md) with the same keys. No need to build `mq_ui`.
- Blocked tool: allow it in **System Settings › Privacy & Security**. Keep the tool folder intact, including `lib/`.
- `usbboot` missing / E102: tools are missing or their architecture doesn't match Python.
- Restore: `./diskos-installer restore-stock`. Keep the saved stock image.
