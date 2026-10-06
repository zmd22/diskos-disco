# diskOS UI dev workflow

How to build, deploy, and iterate on the diskOS UI (`mq_ui`) on real hardware. For iterating on
UI changes you do **not** need the installer or a reflash. Build, push over SSH, hot-reload. A reboot
simply reverts to the flashed build, which is what you want while experimenting.

## 1. Build the binary

Use the pinned Docker builder (works on Linux and macOS, Intel or Apple Silicon):

```sh
cd ui
docker build -t diskos-ui-builder .
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" diskos-ui-builder
```

This produces a static `mq_ui` for `mipsel-linux-musl`. LVGL is vendored, so nothing else is needed.
See [`ui/README.md`](../ui/README.md) for the toolchain details and a from-scratch (non-Docker) build.

## 2. Get a shell on the device

Enable **Debug Mode** on the device (Settings -> System). It shows the device IP and a one-time SSH
password. The password is regenerated on every enable and SSH access does **not** survive a reboot.

```sh
ssh root@<device-ip>      # password from Debug Mode
```

## 3. Push the binary

Stream the freshly built `mq_ui` to a staging path, verify it, then move it into place:

```sh
scp mq_ui root@<device-ip>:/usr/data/mq_ui.new
ssh root@<device-ip> 'chmod 755 /usr/data/mq_ui.new && md5sum /usr/data/mq_ui.new'
# confirm the md5 matches your local build, then:
ssh root@<device-ip> 'mv /usr/data/mq_ui.new /usr/data/mq_ui'
```

You do **not** need to touch the boot manifest for a hot-reload. The manifest and the S97 check only
run at boot; for a live reload they are irrelevant.

## 4. Hot-reload (the important part)

This is where most people get bitten. `fiio_init.sh` runs a watchdog that does `pgrep -x mq_ui`, and
busybox `pgrep -x` matches the full `argv[0]`, not the process `comm`. If you kill `mq_ui` and do not
immediately relaunch a **detached** replacement, the watchdog respawns the **stock**
`/usr/bin/mq_ui`. That is the "it fell back to the stock FiiO UI" you may have seen.

Two rules:

### Never kill `mq_player`

`mq_player` is the stock audio engine. Killing it frees the SD card, and the hardware MCU reboots the
whole device (about 10 seconds). Only ever touch `mq_ui`. If a restart caused a full reboot rather than
just the stock UI appearing, an errant `mq_player` kill is the usual cause.

### Relaunch detached, from `/usr/data`, in one step

```sh
killall -9 mq_ui; setsid /usr/data/mq_ui </dev/null >/usr/data/diskos_boot.log 2>&1 &
```

The `setsid </dev/null` is what matters. Without it the new process is a child of your SSH session and
dies when the session closes, so the watchdog wins the race and respawns stock. Then confirm your build
(not stock) is the one running:

```sh
for p in $(pidof mq_ui); do
  [ "$(readlink /proc/$p/exe)" = /usr/bin/mq_ui ] && kill -9 $p    # prune only a stock instance
done
for p in $(pidof mq_ui); do readlink /proc/$p/exe; done    # should print /usr/data/mq_ui
```

The binary built from this source already normalizes its own `argv[0]` to bare `mq_ui`, so once it is
relaunched from `/usr/data/mq_ui` the watchdog is satisfied and leaves it alone.

The snippet above is the quick manual form; it prunes stock right away and relies on the watchdog to
recover if your build does not start. For scripted use prefer [`../tools/diskos-deploy.sh`](../tools/diskos-deploy.sh),
which does the same thing but polls until your build is confirmed running before pruning stock, and
fails safely (leaving the stock UI as a fallback) if your build never comes up.

To go back to stock without a reboot, just `killall mq_ui` and let the watchdog respawn the stock UI.

## 5. Make it permanent (optional)

Hand-deployed binaries revert to the flashed build on reboot (S97 verifies `/usr/data/mq_ui` against
the read-only manifest). To bake a build in permanently, flash it with the installer:

```sh
./diskos-installer install --firmware SNOWSKY_DISC_update_*.zip --ui path/to/mq_ui --variant public
```

`--ui` overrides the bundled UI binary, so you can flash your own build. This is a mask-ROM write and
takes about 20 minutes. The installer prompts for confirmation before it writes; add `-y` only when you
deliberately want to skip that prompt (for example in a script), since it rewrites the root filesystem.

### macOS note

The installer looks for host-native tools under `vendor/<host-tag>/` (for example
`vendor/macos-arm64/`), not on your `PATH`. An Apple Silicon release package includes prebuilt flash tools; for a source checkout or an Intel Mac, build the native tools once:

```sh
./vendor/setup-macos.sh
```

It builds `usbboot`, `mksquashfs`, and `unsquashfs` (with load paths rewritten) into
`vendor/macos-arm64/` from `src/usbboot` plus Homebrew deps (`libusb squashfs lzo dylibbundler`) and the
Xcode command line tools. After that the installer finds them and the `[E102]` error goes away. Apple Silicon release users need Python 3 and the setup dependencies, but do not need to build these flash tools.

## Notes

- **Screenshots:** the device ships `fbshot`, which writes the framebuffer to `/usr/data/fb.raw`
  (360x360, 32bpp BGRA, panel rotated 180 degrees). Pull it and convert to view.
- **Player logs:** `/usr/data/fiio/log/fiio_player.log`. **PCM state:**
  `/proc/asound/card*/pcm*p/sub*/status` (`RUNNING` = playing).
- **Raw player frames** (for debugging IPC): `/usr/data/psend <FRAME>` sends a raw `/player` command.

## Preview a UI build without reflashing

A live preview is temporary. Build `mq_ui` as above, keep the Disc and computer on the same Wi-Fi network, and enable Debug Mode under Settings > System. The screen shows the Disc IP and a fresh SSH password. You may enter it interactively; `sshpass` is optional.

From the folder holding `mq_ui`, stream it to a staging path and compare the two MD5 hashes before replacing the running UI:

```sh
ssh root@<device-ip> 'cat > /usr/data/mq_ui.new && chmod 755 /usr/data/mq_ui.new && md5sum /usr/data/mq_ui.new' < mq_ui
md5sum mq_ui
ssh root@<device-ip> 'mv /usr/data/mq_ui.new /usr/data/mq_ui'
```

Run the detached reload command in section 4, then check `/proc/<pid>/exe` as shown there. The startup animation runs again. Restarting the Disc restores the flashed build because the boot script checks the UI against its read-only manifest. To keep a build across restarts, flash it with the installer as in section 5.
