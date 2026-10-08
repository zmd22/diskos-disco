# Updating from the SD card

[Back to the README](../README.md)

Flash once with your key, then every new UI build goes onto the SD card. No reflashing, no SSH.

## How the trust works

```
root.key  ──signs──▶  keyauthz  ("this leaf key may sign updates")
                          │
leaf.key  ──signs──▶  update.manifest  ("this mq_ui, this sha256, this epoch")
                          │
Disc:  baked root.pub.pem ─▶ checks keyauthz ─▶ checks manifest ─▶ checks mq_ui ─▶ installs as a trial
```

| File | Secret? | Where it goes |
|---|---|---|
| `root.key` | **yes** | stays offline (USB stick, password manager) |
| `leaf.key` | **yes** | stays on your computer — signs every build |
| `diskos-root.pub.pem` (= `root.pub.pem`) | no | baked into the Disc **once**, at flash time |
| `leafpub.pem`, `keyauthz`, `keyauthz.sig` | no | copied into every update folder |

> [!IMPORTANT]
> Only **public** `.pem` files ever go to the Disc. Lose `root.key`/`leaf.key` and you can't sign
> updates any more until you reflash with new keys — back them up.
> `diskos-keys/` is in `.gitignore`; keep it that way.

## One time: keys + flash

```sh
sh ui/tools/owner-keys/diskos-keys.sh           # -> ./diskos-keys/ (refuses to overwrite existing keys)
cp diskos-keys/diskos-root.pub.pem payload/
./diskos-installer install --firmware <V2.57 zip> --variant public \
  --ui payload/mq_ui --ota-key payload/diskos-root.pub.pem
```

At the confirmation prompt the installer prints `ota: ON (… key sha256 xxxxxxxxxxxxxxxx…)`.
That must be the start of `sha256sum diskos-keys/diskos-root.pub.pem`. If it says `OFF` or shows another
hash, answer **N** — nothing is written.

## Every build

```sh
sh ui/tools/owner-keys/diskos-sign.sh ui/mq_ui 1.2.0    # [version] [keys-folder] [out-folder]
```

1. Copy the whole `diskos-update/` folder to the **root** of the SD card (`<SD>/diskos-update/`).
2. On the Disc: **Settings › System › Update from SD Card › Update**.
3. Restart. The new UI starts as a **trial** — keep it, or go back. If it fails to start, the Disc rolls
   back on its own.

Each signing gets a newer **epoch** (minutes since 2026-01-01), so the Disc never re-installs the same or
an older build.

## Check the Disc

```sh
ssh root@<disc-ip> "md5sum /usr/data/mq_ui; ls /etc/diskos-ota/; sha256sum /etc/diskos-ota/root.pub.pem"
```

Expect your build's md5, the three files `root.pub.pem  epochs  diskos-verify.sh`, and your key hash.

## Troubleshooting

| You see | Means | Fix |
|---|---|---|
| "this install has no update key" | flashed without a root key (`ota: OFF`) | reflash with `--ota-key` |
| old UI after flashing | the installer used another `payload/mq_ui` | pass `--ui` explicitly, check its md5 first |
| `ota: ON` but "not my key" | you compared against `leafpub.pem` or `root.key` | compare with `diskos-root.pub.pem` |
| update refused | signed with keys from another `diskos-keys/` folder | sign with the folder whose `root.pub.pem` matches the Disc |
| `Exec format error` running your script | missing `#!/bin/sh` first line | add it, or run `sh script.sh` |
| copied `mq_ui` over SSH → stock UI boots | boot check quarantined an unsigned binary | use the SD route |

Verify a bundle on your computer before copying it:

```sh
sh payload/diskos-verify.sh diskos-keys/diskos-root.pub.pem 1 1 diskos-update diskos-update/mq_ui
# OK component=mq_ui version=1.0 epoch=… keyepoch=1
```
