# Updating from the SD card

[README](../README.md) · [Developer workflow](DEV_WORKFLOW.md)

> Hobby project, as-is, no warranty. Not affiliated with FiiO or Snowsky. Back up your music and keep the stock image.

Flash once with your public update key; after that, interface builds install from the SD card with no reflash or SSH.
The Disc verifies signatures, boots the new build as a trial and rolls back if it fails.

On Windows, the [Windows app](WINDOWS.md) handles keys, flashing and signing (its Update tab covers steps 2–4). This
guide covers the Linux CLI and the underlying process.

> [!IMPORTANT]
> **On 1.4.0 or older, reflash once** with the same `--ota-key` (step 1). The 1.4.1 fixes live in the boot script,
> which SD updates can't replace. From 1.4.1 on, every release is a normal SD update.

An SD update replaces `mq_ui` (the interface, including built-in apps such as [Disc Shelf](DISC_SHELF_INSTALL.md)).
It doesn't touch the stock player, boot scripts or separately installed legacy apps. A downloaded `mq_ui` is unsigned
until you sign it (step 3).

## Notes

- **3.6.1** is a normal SD update.
- **3.6.0** is a normal SD update.
- **3.5.1** is a normal SD update.
- **3.5.0** is a normal SD update.
- **3.4.4** is a normal SD update.
- **3.4.3** is a normal SD update.
- **3.4.2** is a normal SD update.
- **3.4.1** is a normal SD update. Afterwards Music Assistant shows the Disc as a new Sendspin player (its ID is now
  its key); remove the old entry.
- Always sign with the key the Disc was flashed with. Themes and preferences are kept.
- `mq_ui` and `mq_ui.build.json` must come from the same release.
- Low on internal storage: remove unused source archives, not active or rollback update directories.
- Lost the key: see [Switching](SWITCHING.md#common-situations).

## 1. Make keys and flash once

Already flashed with a key? Reuse that keys folder and skip to step 2; an existing install won't trust a new key.

From the repo root, starting from stock V2.57:

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

With the Disc off, hold Volume Down and connect USB when prompted. Confirm only if `ota: ON` shows the hash printed
above; otherwise answer **N**. Use `public`: `dev`'s USB serial gadget disables USB storage.

| File | Handling |
|---|---|
| `root.key` | Private, offline backup. Only used to authorize leaf keys. |
| `leaf.key` | Private, backed up. Signs every update. |
| `diskos-root.pub.pem` / `root.pub.pem` | Public; installed on the Disc. |
| `leafpub.pem`, `keyauthz`, `keyauthz.sig` | Public; included in every package. |

Back up the whole folder. Keep private keys out of the repo, downloads and the SD card. A different root key means
reflashing, so restore from backup rather than generating new keys to fix a rejected update.

## 2. Get and verify `mq_ui`

Download `mq_ui` and `SHA256SUMS` from the [latest release](https://github.com/zmd22/diskos-disco/releases/latest):

```sh
awk '$2 == "mq_ui" { print }' SHA256SUMS | sha256sum -c -   # expect: mq_ui: OK
```

Self-built: `ui/mq_ui` ([DEV_WORKFLOW.md](DEV_WORKFLOW.md)). Never put `host-render` on the Disc.

## 3. Sign

```sh
sh ui/tools/owner-keys/diskos-sign.sh path/to/mq_ui 3.6.1    # keys in ./diskos-keys, output ./diskos-update/
```

Full form: `diskos-sign.sh <binary> [label] [keys-folder] [output-folder]`. The label (≤32 chars of `A-Za-z0-9._-`)
only names the package; About shows the version compiled into `mq_ui`. The output folder is wiped first, so never
point it at keys or source.

**Epochs:** each package carries whole UTC minutes since 2026-01-01, from your clock. The Disc accepts only epochs
higher than the last accepted one; labels don't matter. Keep your clock correct; to retest, re-sign a minute later.

Optional integrity check (doesn't prove the key matches your Disc):

```sh
sh payload/diskos-verify.sh /path/to/diskos-keys/diskos-root.pub.pem 1 1 ./diskos-update ./diskos-update/mq_ui
# expect: OK component=mq_ui ...
```

## 4. Copy to the card

The six files from one signing run go in `<SD>/diskos-update/` (not nested): `mq_ui`, `update.manifest`,
`update.manifest.sig`, `leafpub.pem`, `keyauthz`, `keyauthz.sig`. Never mix runs.

> [!IMPORTANT]
> Copy the files individually, overwriting the old ones; don't copy the folder over an existing one. On some exFAT
> cards deleting the old folder silently fails and the new one ends up nested inside it. The Linux and Windows apps
> copy file by file.

Eject safely. If you used the Disc's USB storage mode, eject on the computer, then switch back to Playback.

## 5. Install and accept

1. **Settings › System › Maintenance › Update from SD Card › Update**; restart when asked.
2. Check version and build ID in **Settings › Help › About**.
3. After three minutes, actual playback accepts the build automatically; otherwise answer **Keep** (or **Go back**).
   Browsing alone doesn't count.
4. Restart once more to confirm.

An unproven trial gets two boots before rollback; repeated crashes roll back sooner. Rollback returns to the previous
kept build, then the flashed one, then stock. Avoid repeated restarts during the trial.

## Troubleshooting

| Symptom | Fix |
|---|---|
| No update key installed | Reflash with `--ota-key`; signing can't add trust. |
| Switches to FiiO's UI during use | Disco! exited; see `/usr/data/diskos_crash.log` and attach its tail to an issue. |
| Boots stock after every install | Old "Default UI: Stock" survives flashing. Volume Up at power-on, then **Settings › System › Controls › Default UI › diskOS**. |
| Odd behaviour after upstream diskOS | **Settings › System › Maintenance › Reset diskOS Settings**. Restoring stock doesn't clear diskOS data. |
| Update folder not found | `diskos-update` must be at the card root with all six files. |
| Refused / damaged | Verify on the computer, compare root key hashes (below), recopy, check the epoch. |
| Same or older epoch | Compare the device's accepted floor with the package epoch; waiting won't fix a floor mismatch. |
| "This exact build was rolled back before" | Rolled-back builds are recorded in `updates/rejected`; re-signing won't help. Use a new build. |
| Not enough space | The Disc's data partition is full; SD space is irrelevant. |
| Old build after a trial | Trial rolled back or wasn't accepted; check About. |
| Old build after flashing | Check the `--ui` file. Flashing clears staged SD builds. |

With **Settings › System › Maintenance › Debug Mode** on:

```sh
DISKOS_IP=192.168.1.50  # shown in Debug Mode
ssh "root@$DISKOS_IP" 'cat /usr/data/updates/last_result'
ssh "root@$DISKOS_IP" 'cat /tmp/.diskos_run; cat /usr/data/updates/accepted; cat /usr/data/updates/rejected'
ssh "root@$DISKOS_IP" 'tail -n 30 /usr/data/diskos_install.log'
ssh "root@$DISKOS_IP" 'sha256sum /etc/diskos-ota/root.pub.pem'
```

`/tmp/.diskos_run` describes the running build; `rejected` lists rolled-back builds. The root hash must match
`sha256sum diskos-keys/diskos-root.pub.pem`. Some files appear only after the first update. Never copy `mq_ui` over
SSH or edit the boot manifest: the Disc rejects it and falls back to stock.

## Returning to stock

**Settings › System › Controls › Default UI › Stock** sets FiiO's UI as default; Volume Up at power-on boots the
other once. Boot scripts and root keys change only by reflashing. See [Going back](../README.md#going-back) and
[Switching](SWITCHING.md).

## Signing limitation

`diskos-sign.sh` uses UTC minutes since 2026, raised above `keys/last-epoch` if needed. It doesn't read the Disc's
accepted or baked floor, so a Disc with a higher floor can reject a package from a different keys-folder history
even with a newer label. Compare the manifest epoch with the device floor before installing, and keep one keys
folder with a monotonic epoch history. Never lower verification floors or edit a signed manifest.
