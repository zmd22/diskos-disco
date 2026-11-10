# Disc Shelf

Built into Disco!: browse a **Navidrome** library or an **SMB3** share and download original files to the SD card.

[README](../README.md) · [How-to](HOWTO.md) · [Tour](DISCO_TOUR.md#apps)

## Setup

1. Open **Disc Shelf** from the menu, All apps or an app shortcut.
2. **Open Disc Shelf** on the preview. Nothing connects until you do.
3. **Server** › **Navidrome** or **SMB3**. Profiles are independent; only the selected one connects.
4. Fill in the fields below, pick the SD download folder, save.

Existing Navidrome config in `/usr/data/apps/disc-shelf/shelf.conf` is still read. Password input is masked.

## Navidrome

- **Server:** full URL with scheme, and port/base path if needed. Prefer HTTPS off your LAN.
- **Username / password**, **Download folder**.

Browse by artist, album and track. Downloads fetch originals (no transcoding).

## SMB3 (experimental)

SMB3 support is experimental. Back up first.
Bug reports: include NAS/server version, never passwords or auth URLs.

- **Host or IP:** optional `:port`, no `smb://`.
- **Share:** share name only, no path.
- **Username / password:** ideally a read-only account.
- **Domain:** optional.
- **Download folder.**

Requires SMB3 with encryption; no SMB1/2 fallback. Plain folder browser: no artist/album view, no streaming.
Selecting a folder includes its subtree, layout preserved. Remote links are skipped. Over-long paths, deep trees or
oversized listings fail rather than truncate.

### Saving on older SD filesystems

Some older FAT/exFAT drivers cannot move a completed temporary download into place without replacing an
existing file. Disc Shelf then exclusively creates a new destination, copies the completed download into it,
and flushes it before recording success. Existing files are never overwritten by this fallback. The progress
page shows **Saving ... to SD...**; failures identify the operation and retain its original error through cleanup.

This save step needs space for a second copy of the current file plus the usual reserve. Its destination name
is visible while copying. Cancellation and I/O failures remove the partial file when it still belongs to this
download; power loss can leave an incomplete, unrecorded destination. Remove that specific incomplete file
before retrying. Unrecorded files are deliberately not overwritten automatically.

## File names

Navidrome: `Artist/YYYY - Album/NN. Track name.ext` (e.g. `Björk/1997 - Homogenic/01. Hunter.flac`). Missing year
is `0000`; collisions get a short suffix instead of overwriting. Unsafe characters are replaced, Unicode kept, track
numbers >99 and multi-disc duplicates handled. Previously downloaded files keep their paths. SMB keeps original names.

## Selecting and downloading

Swipe from the middle of a row to start selecting (a first-use hint follows your theme's hint setting), tap more
circles, then **Download**.

- **Missing only:** skip existing files, complete partial albums.
- **Download again:** replace only files Disc Shelf recorded as its own for this source; other files are untouched.
- Markers: check = selected and missing; SD icon = selected and already local. Partial matches never grant overwrite.
- A–Z grid, letter circle and pagination dots are available; long names scroll.

The progress page shows the file name (up to three lines), per-file percentage, rolling speed, a progress bar and
MiB transferred/total. All figures are per file, not per job.

The **D!** circle on the right edge opens a themed now-playing overlay (title, artist, progress, Previous,
Play/Pause, Next). **Close** or Back dismisses it; the download continues. It adds no workers, timers, decodes or
network requests.

While copying, Home and Back are locked and the header arrow doesn't cancel. Use **Cancel**; navigation unlocks once
cleanup finishes. Quick Settings and the screensaver still work. There's no background downloading.

Cancel keeps completed files and deletes the partial one. Replacements keep the old file until the new one passes
checks. No library rescan is triggered; use Disco's normal indexing.

## Credentials

- **Remember password** defaults to **Off**: the password lives in memory for the session and is cleared on exit
  (survives worker restarts on save).
- **On** stores it **in plain text** in a permission-restricted config file. Root access or a firmware backup exposes
  it. Existing saved Navidrome passwords stay saved until you change the setting.
- SMB3 is encrypted end to end. Navidrome uses a salted token rather than the password, but the token is reusable,
  and HTTP, helper arguments or proxy logs can leak it. Use HTTPS.

## Power, speed, storage

- During a copy, Wi-Fi power saving is disabled if it was on, and restored afterwards (best-effort; a killed process
  can't restore it). Downloads block idle power-off.
- Nothing runs while the app is closed: no discovery, sync or polling.
- Navidrome writes go through a 128 KiB buffer: ~75% fewer write calls in host tests, but no measured on-device
  speedup. Final sync and low-space reserve checks remain.
- Config lives in internal storage. Presence/ownership catalogues live in the destination's `.shelf` directory;
  don't touch it while the app is open, or safe replacement may be lost.

## Legacy standalone app

**Disc Shelf 1.0.4** (Navidrome only) was last shipped with
[3.3.1](https://github.com/zmd22/diskos-disco/releases/tag/v3.3.1). Disco hides duplicate standalone menu entries. Use
the built-in app and keep your existing config rather than reinstalling the standalone binary.

[Implementation and limits](../archive/docs/plans/disc-shelf-smb3.md) · [Validation notes](../archive/docs/performance/disc-shelf-native.md)
