# diskOS device tools

Host scripts for screenshots and touch input over SSH. Install builds via [signed SD updates](../docs/UPDATING.md);
full workflow in [DEV_WORKFLOW.md](../docs/DEV_WORKFLOW.md). **Read [Device rules](#device-rules) first.**

## Contents
| Script | What it does |
|---|---|
| `diskos-shot.sh` | Capture the device screen to a PNG |
| `diskos-touch.sh` | Tap / swipe via the on-device injector |
| `tinj.c` | On-device touch injector (build once, push) |

## Requirements
- `sshpass` and `ssh` (OpenSSH)
- For screenshots: `python3` with Pillow (`pip install Pillow`)

## Getting access
Enable **Settings › System › Maintenance › Debug Mode**; it shows the IP and an SSH password that changes on every
enable and doesn't survive reboot. Pass both via the environment:

```sh
export DISKOS_IP=192.168.x.x       # from Debug Mode
export DISKOS_PW=xxxxxxxxxx         # from Debug Mode
```

## Install a UI build

Sign with your existing keys, copy the six `diskos-update` files to the card root, install from Settings, restart,
accept the trial ([details](../docs/UPDATING.md)). This doesn't install separate user apps.

## Screenshot
```sh
DISKOS_IP=... DISKOS_PW=... ./diskos-shot.sh home.png
```
Reads one 518,400-byte frame from `/dev/fb0` (360×360 BGRA, panel mounted 180°), rotates it upright and saves a PNG.

## Touch
Coordinates are 0–359 from top-left, matching the upright screenshot; the injector handles rotation.

Build and push the injector once:
```sh
# from ui/ where the Docker image lives, with tinj.c copied in:
docker run --rm -v "$PWD:/src" -w /src diskos-ui-builder sh -c '${CROSS}gcc -O2 -static -o tinj tinj.c'
export SSHPASS="$DISKOS_PW"    # sshpass -e reads the password from the environment, not the argv
sshpass -e ssh root@"$DISKOS_IP" 'cat > /usr/data/tinj && chmod 755 /usr/data/tinj' < tinj
```

Use:
```sh
DISKOS_IP=... DISKOS_PW=... ./diskos-touch.sh tap 180 116          # tap the album cover
DISKOS_IP=... DISKOS_PW=... ./diskos-touch.sh swipe 40 125 320 125  # swipe right (back / previous)
```

Typical loop:
```sh
./diskos-shot.sh state.png     # look at the screen
./diskos-touch.sh tap X Y      # act on it
./diskos-shot.sh state.png     # confirm the result
```

Notes and limits:
- The cst816t is MT type-B on `/dev/input/event1`; `tinj` writes 32-bit `input_event`s there. Physical keys are on
  `event0`; the player reads it without a grab, so a written key reaches it (Lock on timeout sends the power key).
- Taps hold ~90 ms. Small buttons are occasionally missed if press and release land in one input cycle; use a list
  row or swipe instead.
- Horizontal swipes change pages; keep `y` near centre to avoid the Now Playing seek ring.

## Device rules

- **Never kill `mq_player`.** It owns the SD card; killing it makes the MCU reboot the Disc (~10 s).
- **Deploy only via signed SD updates.** Don't replace `/usr/data/mq_ui` over SSH or edit the boot manifest; boot
  verification falls back to stock.
- Use the normal restart/shutdown controls; these tools never need to stop services.

## Playback / IPC debugging
- Player log: `/usr/data/fiio/log/fiio_player.log`
- PCM state: `/proc/asound/card*/pcm*p/sub*/status` (`RUNNING` = playing)
- Raw `/player` frame: `/usr/data/psend <FRAME>` (on device)

## Notes
- 2.4 GHz Wi-Fi only; SSH drops occasionally. Scripts use short timeouts; just rerun.
- No credentials are hardcoded.
