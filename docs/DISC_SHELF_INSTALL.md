# Disc Shelf — your music, to go

Built into **diskOS Disco! 3.3.0**. Browse a **Navidrome** library or an **SMB3** share and download original files to your SD card. There is no separate app to install on Disco 3.3.0.

[Home](../README.md) · [How-to](HOWTO.md) · [Tour](DISCO_TOUR.md#disc-shelf)

## Start here

1. Open **Disc Shelf** from your menu, All apps or an assigned app shortcut.
2. Press **Open Disc Shelf** on its preview. Browsing past the preview does not connect to a server.
3. Press **Server** and choose **Navidrome** or **SMB3**. Profiles are separate; only the selected source connects.
4. Enter the details below, choose the SD download folder and save.

Existing Navidrome settings are read from `/usr/data/apps/disc-shelf/shelf.conf` and remain compatible. Configure the source through the native keyboard; password input is masked.

## Navidrome

- **Server:** the full server URL, including `http://` or `https://` and the port/base path if needed.
- **Username / password:** your server account.
- **Download folder:** where new music should be saved on the SD card.

Prefer HTTPS, especially beyond a trusted local network. Browse artists, albums and tracks. Downloads request original files, without asking Navidrome to transcode them.

## SMB3 — experimental

SMB3 passed encrypted host wire tests, but **has not yet been validated against a physical NAS on the Disc**. Back up important local files before trying it. Report the NAS/server version and what happened if it fails; never share passwords or authentication URLs.

- **Host or IP:** the server name/IP, optionally `host:port`. Do not add `smb://`.
- **Share:** the share name, separately from the host. Do not put a folder path here.
- **Username / password:** an account allowed to read that share. A dedicated read-only account is a good fit.
- **Domain:** optional; leave blank if your server does not require one.
- **Download folder:** the destination on the SD card.

The server must support SMB3 with encryption. There is no SMB1/SMB2 fallback. This is a folders-and-files browser, with no artist/album separation and no network playback. A folder selection includes its descendants and preserves their layout. Reported remote links are skipped. Oversized paths, very deep trees or listings beyond the safety limits fail instead of being silently truncated.

## File and folder names

New Navidrome downloads use `Artist/YYYY - Album/NN. Track name.ext`. Missing years use `0000`; real collisions receive a short suffix instead of overwriting another file. Existing downloaded files keep their paths and safe replacement records. SMB preserves the share’s original names.

Example: `Björk/1997 - Homogenic/01. Hunter.flac`. Album metadata supplies the year; no extra request is needed. Filename-unsafe characters are replaced, Unicode is retained, and track numbers above 99 remain intact. Repeated titles on different discs remain separate.

## Select and download

Swipe from the middle of a row to enter selection. A first-use hint explains this gesture; it follows your theme’s hint settings. Tap the selection circles for more items, then **Download**. This selection menu offers downloads rather than Library playback/queue actions.

- **Missing only:** keep existing copies and finish what is missing, including partial albums.
- **Download again:** replace only matching files recorded as owned by Disc Shelf for that source. Unrelated existing files are kept.
- The existing local-presence indicator is retained. A selected missing item shows a check; an already-local selected item shows an SD-card marker. Partial/present-only entries do not grant overwrite permission.
- Both the A–Z grid and letter circle are available, along with the list-position/pagination dots. Long selected names scroll.

The download page shows up to three text lines with ellipsis, the current file’s percentage and rolling speed, a progress bar, and transferred/total MiB. These are **per-file**, not a total-job percentage. The speed settles after the first sample and resets for each file.

The small **D! circle on the right edge** opens a music overlay matching your theme. It shows separate scrolling title and artist lines, progress, Previous, Play/Pause and Next. **Close**, or the Back gesture, dismisses the overlay and returns to the copy. Music controls use Disco's existing playback commands; the download continues underneath.

During a copy, **Home and Back cannot exit Disc Shelf**, and the header Back arrow does not cancel it. Use **Cancel** to stop copying; navigation stays locked until the worker has finished cleanup. Quick Settings and the screensaver remain available. This is a local overlay, not background downloading or an app switcher. It adds no new worker, polling timer, artwork decode or network request; its labels refresh only through the existing visible-app update cycle.

Cancel keeps completed files and removes the incomplete temporary file. A replacement retains the old copy until the new one has completed and passed checks. No automatic full-library rescan is triggered: use Disco’s normal indexing after adding new music.

## Passwords and privacy

Fresh profiles default to **Remember password: Off**. The password is retained in memory for the current app session and cleared when Disc Shelf closes. Saving source settings can restart its worker without losing the session password.

**Remember password: On stores readable credentials in a private configuration file.** Directory/file permissions restrict ordinary access, but the file is not encrypted at rest. Root access or a firmware backup can expose it. Existing saved Navidrome passwords remain remembered until you change this setting.

SMB3 traffic is encrypted and credentials stay inside the process. Navidrome uses a random salted token instead of sending a raw password, but that token remains reusable. HTTP traffic, authenticated helper arguments and server/proxy logs can expose credentials. Prefer HTTPS and keep config files and authenticated URLs private.

## Battery, speed and storage

During a copy, Disc Shelf temporarily disables Wi-Fi power saving if it was enabled, then restores its previous mode after success, cancellation or errors. If it was already disabled, it stays unchanged. Unsupported driver commands are skipped; restoration is best-effort if the driver becomes unavailable. Forced process termination cannot guarantee restoration. This adds no polling, reconnects or permanent preferences.

There is no discovery, periodic sync or remote browsing while the app is closed. The worker waits when idle; transfer checks and cancellation remain active. A download intentionally prevents idle power-off until it finishes or you cancel.

Navidrome now batches small SD writes into a temporary 128 KiB buffer. Host tests show about 75% fewer file-write calls, **not a proven on-device transfer-speed increase**. Your Wi-Fi, server and card still matter. The buffer is released after each request; final file sync and low-space reserve checks remain.

Configuration remains in internal storage. Private local-presence/ownership catalogues are under the destination’s `.shelf` directory. Do not remove or edit these while the app is open; losing ownership records can stop intentional replacement from being offered safely.

## Older firmware

The separate **Disc Shelf 1.0.4** release archive is the legacy Navidrome-only app. It does not contain SMB3. It remains available for older firmware; native Disco hides duplicate standalone menu entries. On 3.2.x, use the built-in app and preserve your existing configuration rather than reinstalling the standalone executable.

[Implementation and limits](../archive/docs/plans/disc-shelf-smb3.md) · [Validation notes](../archive/docs/performance/disc-shelf-native.md)
