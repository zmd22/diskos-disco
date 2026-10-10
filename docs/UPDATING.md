# Updating from the SD card

[Back to the README](../README.md) · [Developer workflow](DEV_WORKFLOW.md)

Flash once with your public update key, then install later **UI builds** from the SD card.
Normal updates need no reflash and no SSH. The Disc verifies the signatures, starts the new
build as a trial, and can return to the previous verified build if the trial fails.

## What an SD update includes

An SD package updates `mq_ui` only. It does not change the stock player, installer or boot
scripts, and does not install or update separate user apps. For Disco! 1.2.2 and later, install Album
Roulette 1.3.1 separately using [these instructions](ALBUM_ROULETTE_INSTALL.md).
A downloaded `mq_ui` or complete release ZIP is **not** already signed with your keys.

The generated folder contains exactly these six files:

| File | Purpose |
|---|---|
| `mq_ui` | The MIPS UI executable |
| `update.manifest` | Component, version label, size, SHA-256 and update epoch |
| `update.manifest.sig` | Leaf-key signature of that manifest |
| `leafpub.pem` | Public key used to check the manifest signature |
| `keyauthz` | Root-authorized permission for the leaf key |
| `keyauthz.sig` | Root-key signature of that authorization |

The installed root public key checks the leaf authorization; the leaf public key checks the
manifest; the manifest's size and hash check the executable. Verification happens while staging
and again at boot. Preserve that chain.

## 1. Set up keys and flash once

**Already installed with an update key?** Reuse the same signing folder and skip to step 2.
Generating a new root key does not make it trusted by an existing install.

For a first install, run from the repository root on your Linux computer:

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

Power the Disc off, hold Volume Down and connect USB when instructed by the installer.
At confirmation, check that `ota: ON` shows the start of the hash you printed. If it says
`OFF` or names another hash, answer **N** and correct the command before flashing.
Use the public variant; the dev USB serial gadget blocks USB storage.

| Key file | Where to keep it |
|---|---|
| `root.key` | Private; back up offline. Authorizes leaf keys. Not needed for daily signing. |
| `leaf.key` | Private; keep on your computer and back it up. Signs UI packages. |
| `diskos-root.pub.pem` / `root.pub.pem` | Public; the root key installed once on the Disc. |
| `leafpub.pem`, `keyauthz`, `keyauthz.sig` | Public; included in each package. |

Keep private keys out of the repository, release downloads and SD card. The whole signing folder
is worth backing up. Daily signing requires `leaf.key`, `leafpub.pem`, `keyauthz` and `keyauthz.sig`;
keep `diskos-root.pub.pem` too for verification and matching the installed key.
If signing credentials are lost, restore the backup; installing a different root key requires
reflashing. Do not regenerate keys to troubleshoot an ordinary rejected update.

## 2. Choose and check the UI binary

Download `mq_ui` and `SHA256SUMS` from the
[release](https://github.com/zmd22/diskos-disco/releases), place them in the same folder, then run:

```sh
awk '$2 == "mq_ui" { print }' SHA256SUMS | sha256sum -c -
```

Expect `mq_ui: OK`. This checks only the UI entry, so other release downloads need not be present.
For a local build, follow [DEV_WORKFLOW.md](DEV_WORKFLOW.md); the result is `ui/mq_ui`.
Never use the desktop `host-render` executable as a device UI.

## 3. Sign with your existing keys

Run from the repository root. This example signs the release's bundled UI with keys stored elsewhere:

```sh
sh ui/tools/owner-keys/diskos-sign.sh \
  payload/mq_ui 1.3.0-Disco \
  /absolute/path/to/your/diskos-keys ./diskos-update
```

Replace `payload/mq_ui` with `ui/mq_ui` for a new build, or the full path of a downloaded binary.
If your keys are in `./diskos-keys`, the short form is:

```sh
sh ui/tools/owner-keys/diskos-sign.sh payload/mq_ui 1.3.0-Disco
```

Arguments are `binary [version-label] [keys-folder] [output-folder]`. The label is at most 32
letters/digits/dots/hyphens/underscores. It labels the package; it does not rewrite the compiled
version shown in About. The script deletes and recreates its output directory: use a dedicated
`diskos-update` folder, never your keys or source folder.

### Update ordering: epochs

The signer calculates the epoch from the computer's UTC clock, in **whole minutes since
2026-01-01**. It does not maintain a counter. Two packages signed in the same minute can have the
same epoch. Once an epoch is accepted, another package must have a higher epoch to install.
Changing only the version label does not bypass this rule.

For a retest, sign again in a later minute and copy the newly generated folder. Keep the computer's
clock correct. An older source version can be deliberately retested if signed with a fresh epoch;
version numbers themselves are not the anti-rollback order.

### Verify on the computer

```sh
sh payload/diskos-verify.sh \
  /absolute/path/to/your/diskos-keys/diskos-root.pub.pem \
  1 1 ./diskos-update ./diskos-update/mq_ui
```

Expect `OK component=mq_ui version=... epoch=... keyepoch=...`. The two `1` arguments are initial
minimum key/UI epochs. This validates the bundle but does not check the Disc's later accepted
epoch or prove that this root key matches the installed one.

## 4. Copy the whole folder to the card

Use your file manager to copy `diskos-update` to the SD card's top level. The final path must be
`<SD>/diskos-update/mq_ui`, not `<SD>/diskos-update/diskos-update/mq_ui`.
Copy all six files together from the same signing run; do not mix packages.

Finish the transfer, then safely eject/unmount the card or USB storage connection. If using the
Disc's USB storage mode, return to Playback after ejecting. Do not edit the folder while the Disc
is reading it.

## 5. Install and check the trial

1. Open **Settings > System > Maintenance > Update from SD Card > Update**.
2. Wait for successful staging and restart when prompted. The running UI is not replaced live.
3. Open **Settings > System > About** and check the expected compiled Disco version and build ID.
4. Keep the trial running for at least **three minutes**. Start a track and check playback,
   navigation and the changed screens.
5. After that minimum time, a confirmed playback start plus running audio can mark the trial
   healthy automatically. Otherwise the UI asks **Keep diskOS <label>?** Choose **Keep** to retain
   it, or **Go back** and restart to revert. Navigation by itself does not accept the build.
6. Restart once after the health proof/Keep choice and confirm the build still runs. Boot promotes
   the healthy trial to the accepted build.

An unproven trial is allowed two trial boots; the following boot rolls it back if it still has no
health proof. A repeatedly crashing trial can be rejected sooner. Avoid repeated restarts before
checking/accepting a trial. If it fails, boot falls back to the previous verified build, or the
flashed baseline when no kept SD build exists. Stock remains the verification-failure fallback.

## Troubleshooting

| Symptom | What to check |
|---|---|
| No update key installed | Reflash once with the intended `--ota-key`; signing alone cannot enable trust. |
| Update folder not found | Folder is named `diskos-update` at the card root; all six files are present. |
| Update refused / damaged | Verify the bundle on the computer, check installed root-key hash, recopy all files, check the epoch. Read `last_result` if available. |
| Same or older epoch | Sign in a later minute with a correct computer clock, then replace the whole card folder. |
| Not enough space | Free space on the Disc's writable data partition; spare space on the SD card alone is insufficient. |
| Old build after a trial | Check whether the trial was accepted or rolled back; check About and boot state. |
| Old build after flashing | Confirm the exact file passed with `--ui`; a newer flash's boot hook clears old SD builds when the flashed UI hash changes. |
| Host verifier passes, Disc refuses | The installed root key or accepted epoch can differ from the host check's key and initial `1 1` floors. |

Optional read-only diagnostics: enable **Settings > System > Maintenance > Debug Mode**, then use SSH with the
address and credentials it displays. Use the current displayed address if it changes.

```sh
DISKOS_IP=192.168.1.50  # replace with the address shown in Debug Mode
ssh "root@$DISKOS_IP" 'cat /usr/data/updates/last_result'
ssh "root@$DISKOS_IP" 'cat /usr/data/updates/run; cat /usr/data/updates/accepted'
ssh "root@$DISKOS_IP" 'sha256sum /etc/diskos-ota/root.pub.pem'
ssh "root@$DISKOS_IP" 'md5sum /usr/data/mq_ui'
```

Compare the root hash with `sha256sum /path/to/your/diskos-keys/diskos-root.pub.pem` on the computer.
Some state files are absent before a first update. `last_result` records staging return code,
source line and available verifier/file error details after the update directory has opened;
earlier failures can leave it missing or stale. Capture the message shown on the Disc too.
Do not overwrite the UI over SSH or alter the boot manifest to get an update accepted.

## Returning to stock or changing the boot chain

**Settings > System > Controls > Default UI > Stock** makes FiiO's UI the default. Holding Volume Up from
power-on selects the other UI once. Use the installer when changing boot scripts or root update
keys; these changes are outside a UI-only SD package. See the [README](../README.md#going-back).
