# diskOS UI development workflow

Build and preview the UI on your computer, then install it through a **signed SD-card update**.
A normal UI update does not require another flash. SSH is for diagnostics, screenshots and
separate user-app installation; it is not the UI deployment route.

## 1. Build or download the UI

For a published version, download `mq_ui` and `SHA256SUMS` from the
[releases](https://github.com/zmd22/diskos-disco/releases). Keep them in one folder and verify
just the UI entry (the checksums file also lists optional release packages):

```sh
awk '$2 == "mq_ui" { print }' SHA256SUMS | sha256sum -c -
```

For a source build, use the MIPS toolchain described in [ui/README.md](../ui/README.md):

```sh
make -C ui CROSS=/opt/mipsel-n2008-musl-cross/bin/mipsel-linux-musl- mq_ui
make -C ui check-theme checkpunct
```

The result is `ui/mq_ui`. The pinned Docker builder is another build route:

```sh
cd ui
docker build -t diskos-ui-builder .
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" diskos-ui-builder
cd ..
```

## 2. Preview on the host

Build the desktop renderer separately from the MIPS build. It shares object paths with the
production target: use a separate checkout for host rendering, or clean before changing toolchains.
The renderer does not run the hardware main loop and does not certify device performance.

```sh
make -C ui clean
make -C ui CROSS= host-render
```

Existing render fixtures and tests are under `ui/tests/`. Use them to inspect layout, readability
and navigation before producing a device build. After rendering in the same checkout, clean
again before compiling for MIPS.

Focused regression checks for Modes, Immersive transitions, editor cancellation, app shortcuts
and safe file replacement:

```sh
make -C ui/tests run review
python3 ui/tests/review_ui_test.py
```

The C tests use AddressSanitizer and UndefinedBehaviorSanitizer. If a restricted host blocks
LeakSanitizer's process inspection, run the C tests with `ASAN_OPTIONS=detect_leaks=0`; the
address and undefined-behavior checks remain enabled.

## 3. Enable signed updates once

If this Disc was already flashed with your public update key, **reuse the existing keys** and
skip this step. A new set cannot sign updates accepted by the old installed root key.

For a first install, generate keys on the computer, back them up, then flash the public variant:

```sh
sh ui/tools/owner-keys/diskos-keys.sh
cp diskos-keys/diskos-root.pub.pem payload/
sha256sum diskos-keys/diskos-root.pub.pem
./diskos-installer install \
  --firmware SNOWSKY_DISC_update_20260909_v257.zip \
  --variant public \
  --ui payload/mq_ui \
  --ota-key payload/diskos-root.pub.pem
```

Before confirming, check that `ota: ON` names the same public-key hash. Private keys stay on
your computer. First-install details and recovery are in the [README](../README.md#install).

## 4. Sign the build

Run from the repository root. Replace the binary and keys paths with your actual paths:

```sh
sh ui/tools/owner-keys/diskos-sign.sh \
  ui/mq_ui 1.3.0-test \
  /absolute/path/to/your/diskos-keys ./diskos-update
```

For a downloaded release, use its `mq_ui` path in place of `ui/mq_ui`. The version argument is a
package label; it does not change the version compiled into Settings > About.
The signing script replaces its output folder, so reserve that folder for generated updates.

Verify the generated signatures and payload before copying:

```sh
sh payload/diskos-verify.sh \
  /absolute/path/to/your/diskos-keys/diskos-root.pub.pem \
  1 1 ./diskos-update ./diskos-update/mq_ui
```

Expect `OK component=mq_ui ...`. The `1 1` values are the initial key/UI epoch floors; this
host check does not replace the Disc's verification against its installed and accepted floors.

## 5. Copy, install and accept

1. Copy the **whole** `diskos-update` folder to the SD card root. The path must be
   `<SD>/diskos-update/mq_ui`, with the five signature/authorization files alongside it.
2. Finish the copy and safely eject/unmount the card or USB storage connection. Return the
   Disc to normal Playback mode if you used USB storage.
3. Open **Settings > System > Maintenance > Update from SD Card > Update**.
4. Wait for successful staging, then restart when prompted. Do not replace files in the
   update folder while installation is reading it.
5. Check **Settings > System > About** for the expected compiled version and build ID.
6. Leave the new build running for at least three minutes. Start a track and check playback,
   navigation and the changed screens. Confirmed playback can automatically mark the trial
   healthy; otherwise choose **Keep** when asked, or **Go back** and restart.
7. Restart once more after acceptance and confirm the expected build still runs. Boot performs
   the promotion of the healthy trial. An unproven trial has a limited boot allowance and rolls back.

The full folder contents, epoch rules and troubleshooting are in [UPDATING.md](UPDATING.md).
UI-only SD updates do **not** install Album Roulette; see
[ALBUM_ROULETTE_INSTALL.md](ALBUM_ROULETTE_INSTALL.md) for that separate user app.

## 6. Diagnose an update without replacing the UI

Enable **Settings > System > Maintenance > Debug Mode** and use the displayed address/credentials.
SSH is optional; it is not required to install an update. These commands only read state:

```sh
DISKOS_IP=192.168.1.50  # replace with the address shown in Debug Mode
ssh "root@$DISKOS_IP" 'cat /usr/data/updates/last_result'
ssh "root@$DISKOS_IP" 'cat /usr/data/updates/run; cat /usr/data/updates/accepted'
ssh "root@$DISKOS_IP" 'sha256sum /etc/diskos-ota/root.pub.pem'
ssh "root@$DISKOS_IP" 'md5sum /usr/data/mq_ui'
```

Some files will be absent before a first update. `last_result` can be absent or reflect an older
attempt if the new attempt failed before staging could open its state directory. Compare the
installed public-key hash with your signing folder's `diskos-root.pub.pem`.
Do not overwrite `/usr/data/mq_ui`, change the boot manifest, or kill `mq_player`.

For screenshots and touch tools, see [tools/README.md](../tools/README.md). Player logs are at
`/usr/data/fiio/log/fiio_player.log`; PCM status is under `/proc/asound/card*/pcm*p/sub*/status`.

## When reflashing is needed

Reflash for the initial install, a different root update key, or installer/boot-chain changes
that a UI-only update cannot carry. Use `--ui` explicitly and check the chosen binary's hash.
Normal `mq_ui` iterations use the SD workflow above. Windows/WSL remains experimental and
unsupported; see [WINDOWS.md](WINDOWS.md).
