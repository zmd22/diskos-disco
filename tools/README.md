# diskOS device tools

Host-side helper scripts for working on a diskOS device over SSH: deploy a locally built UI and
hot-reload it, and capture screenshots. These are the scripts we use for day-to-day iteration. The
full workflow (including building the binary) is in [`../docs/DEV_WORKFLOW.md`](../docs/DEV_WORKFLOW.md).

If you are an agent or a new contributor picking this up cold, read the **Device rules** section
first. A couple of the device's behaviors are non-obvious and easy to get wrong.

## Contents
| Script | What it does |
|---|---|
| `diskos-deploy.sh` | Push a built `mq_ui` to the device, verify it, and hot-reload it safely |
| `diskos-shot.sh` | Capture the device screen to a PNG |
| `diskos-touch.sh` | Drive the touchscreen over SSH (tap / swipe) via the on-device injector |
| `tinj.c` | The on-device touch injector, built once and pushed (used by `diskos-touch.sh`) |

## Requirements
- `sshpass`, `ssh`, `scp` (OpenSSH), and `md5sum`
- For screenshots: `python3` with Pillow (`pip install Pillow`)

## Getting access
Enable **Debug Mode** on the device (Settings > System). It shows the device IP and a one-time SSH
password. The password is regenerated on every enable and **does not survive a reboot**, so grab a
fresh one each session. The scripts read the IP and password from environment variables:

```sh
export DISKOS_IP=192.168.x.x       # from Debug Mode
export DISKOS_PW=xxxxxxxxxx         # from Debug Mode
```

## Deploy + hot-reload
```sh
# from the ui/ build directory, after producing mq_ui:
DISKOS_IP=... DISKOS_PW=... ./diskos-deploy.sh ./mq_ui
```
It streams the binary to a staging path, verifies the md5, moves it into place, and relaunches it
detached. A hand-deployed binary reverts to the flashed build on the next reboot (that is intentional;
it means you can always get back to a known-good state by rebooting). To make a build permanent, flash
it with the installer (`--ui path/to/mq_ui`).

## Screenshot
```sh
DISKOS_IP=... DISKOS_PW=... ./diskos-shot.sh home.png
```
The panel is 360x360, 32bpp BGRA, mounted rotated 180 degrees. The script reads `/dev/fb0` (one frame
is 518400 bytes), reinterprets it as BGRA, rotates 180, and saves a PNG.

## Driving the touchscreen (agent navigation)
This lets an agent navigate the UI: capture a screenshot, read the coordinate of the thing to touch,
and tap it. Coordinates are screen coordinates (0..359, top-left origin) matching the upright PNG from
`diskos-shot.sh`; the injector applies the 180-degree panel rotation for you.

Build the injector once (any host with the Docker builder), then push it to the device:
```sh
# from ui/ where the Docker image lives, with tinj.c copied in:
docker run --rm -v "$PWD:/src" -w /src diskos-ui-builder sh -c '${CROSS}gcc -O2 -static -o tinj tinj.c'
export SSHPASS="$DISKOS_PW"    # sshpass -e reads the password from the environment, not the argv
sshpass -e scp tinj root@"$DISKOS_IP":/usr/data/tinj
sshpass -e ssh  root@"$DISKOS_IP" chmod 755 /usr/data/tinj
```

Then drive it:
```sh
DISKOS_IP=... DISKOS_PW=... ./diskos-touch.sh tap 180 116          # tap the album cover
DISKOS_IP=... DISKOS_PW=... ./diskos-touch.sh swipe 40 125 320 125  # swipe right (back / previous)
```

A typical agent loop:
```sh
./diskos-shot.sh state.png     # look at the screen
./diskos-touch.sh tap X Y      # act on it
./diskos-shot.sh state.png     # confirm the result
```

Notes and limits:
- The cst816t is MT type-B on `/dev/input/event1`. `tinj` writes 32-bit-ABI `input_event` records
  directly to that node. Physical keys (volume, play, power) are on `event0` and owned by the player,
  so this tool cannot press them; it only drives touch.
- Taps reliably wake the screen and drive list rows and swipe navigation. A small non-list button can
  occasionally be missed if press and release fall in one UI input-read cycle (the tap holds contact
  ~90ms to avoid this). If a control resists, use a list-row equivalent or a swipe.
- On the round screen, a horizontal swipe changes pages; `y` near the center avoids the seek ring on
  Now Playing.

## Device rules (read before poking around)

- **Never kill `mq_player`.** It is the stock audio engine and it owns the SD card. Killing it frees
  the card, and the hardware MCU reboots the whole device (about 10 seconds). Only ever touch `mq_ui`.
- **The UI watchdog respawns stock if you kill `mq_ui` without relaunching.** `fiio_init.sh` runs a
  loop that does `pgrep -x mq_ui`, and busybox `pgrep -x` matches the full `argv[0]`. If you kill
  `mq_ui` and do not immediately relaunch a **detached** replacement, it respawns the stock
  `/usr/bin/mq_ui`. `diskos-deploy.sh` handles this (relaunch with `setsid </dev/null`, then prune any
  stock instance). To return to stock on purpose, just `killall mq_ui` and let the watchdog respawn it.
- **Hand-deploys are not persistent.** The S97 boot hook verifies `/usr/data/mq_ui` against a
  read-only manifest and restores the flashed build on reboot. Iterate freely; reboot to reset.

## Debugging playback / IPC
- Player log: `/usr/data/fiio/log/fiio_player.log`
- PCM state: `/proc/asound/card*/pcm*p/sub*/status` (`RUNNING` means audio is actually playing)
- Send a raw `/player` command frame: `/usr/data/psend <FRAME>` (on the device)

## Notes
- The device is 2.4 GHz Wi-Fi only; SSH can blip occasionally. The scripts use short connect timeouts;
  just re-run on a transient failure.
- These scripts do not hardcode any credentials. Debug Mode passwords are per-session by design; pass
  them in through the environment as shown above.
