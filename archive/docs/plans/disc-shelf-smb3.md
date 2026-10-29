> Historical material. Not current 3.2.1 instructions; see the current documentation index.

# Disc Shelf: SMB3 source integration

Shipped as Disco 2.7.0, based on public 2.6.2 plus the native Disc Shelf integration. SMB3 remains experimental; the validation limits below still apply.

## Behaviour

The Server screen has a Navidrome / SMB3 chooser. Profiles are kept separately, but only the saved, selected source starts a worker connection. Saving changes cancels and collects the previous worker before starting the new one. The Disco menu preview is retained: browsing past Disc Shelf starts neither source. Press Open Disc Shelf to enter the app.

For SMB3, configure a host/IP (optionally host:port), a share name, username, password, optional domain, and an SD-card download folder. The share is browsed as folders and files. There is no artist/album/tag interpretation or streaming playback. Folder names end in `/`. Selection, Download-only actions, themed rows, position indicators, A–Z grid/ring and Back reuse the native browser. Folder nesting is limited to 32 levels and paths to 1023 UTF-8 bytes; oversized listings fail rather than silently truncate. Reported remote links/reparse points are omitted.

Downloads keep the share's folder structure below the chosen SD folder. Selecting a folder recursively copies its contents, including empty files and folders. Existing files are kept by default. Download again may replace only a file previously downloaded by this source/account into this destination, recorded in its manifest. An unrelated pre-existing file is kept even when Download again is selected. Presence of such a file is marked as partial/present, not verified synced.

New data goes into an exclusive temporary file in the destination directory. Completed data is checked against the open remote file's size, identity and modification time, flushed, and published atomically. A failed or cancelled replacement preserves the original. New copies refuse a concurrently-created target using Linux no-replace rename (compatible with modern FAT/exFAT drivers); a hard-link fallback supports older POSIX filesystems. A card lacking both atomic operations fails safely rather than overwriting a different file. The root `.shelf` directory is reserved for application metadata. Destination components are opened relative to the SD root with no-follow checks. No remote write operations or system share mounts are used.

## Work and memory

Pinned libsmb2 6.3 (`2bcb014b23a8c8cccf534eea8bda2fe5df453cfd`) provides the protocol. SMB3 and encrypted transport are required; there is no SMB1/2 fallback. Signing capability is advertised explicitly. Credentials stay inside the process, not shell commands or SMB helper arguments.

Directory responses are requested in 64 KiB chunks and inserted into a disk-backed SQLite index. The UI retains its 32-row pool. File reads use a 32 KiB buffer. The shared context remains 612,448 bytes on the host. Jobs are disk-backed, with a 100,000-entry/job bound and storage/memory checks. No tag database scan is needed for SMB. Network polling runs on the worker only while a requested operation is outstanding, with cancellation checks at most 100 ms apart during polling. Name resolution is provided by the system resolver and may block separately; use a numeric IP if local DNS is unreliable.

The worker sleeps on a condition variable between user commands; there is no periodic scan, keepalive timer or background synchronisation. Closing cancels the owned connection, releases the SD lease and collects memory. Existing screen-off download and deliberate shutdown handling are retained.

## Credentials and privacy

Fresh profiles default to **Remember password: Off**. Passwords remain in memory only for the app session; closing Disc Shelf clears its session cache. Saving server settings and reopening the worker within that session retains the entered password. Existing Navidrome configurations containing a saved password keep working and are treated as Remember password On until changed.

With Remember password On, passwords are readable in `/usr/data/apps/disc-shelf/shelf.conf`. The app directory is mode 0700 and the atomic configuration file mode 0600. These permissions are access control, not encryption. Anyone with root, a firmware backup or access to the flash can read remembered passwords. The app masks password fields and does not log them. No device hardware-backed secret store is assumed; encrypting with a firmware-embedded key would not solve this threat.

SMB3 traffic is encrypted and uses the in-process client. Prefer a dedicated NAS account with read-only access to the required share.

Navidrome now uses fresh random salted Subsonic token authentication instead of placing the raw password in the URL. **A captured token/salt is still a reusable credential, not an expiring token.** The existing wget transport still has authenticated URLs in process arguments, and server/proxy logs may record them. Use HTTPS; plain HTTP exposes traffic and tokens. HTTPS protects transport, not local root/process access or server logging. Do not publish logs/process dumps containing authentication URLs. The metadata catalogue contains remote names and paths on the SD card, but no passwords.

## Source changes

- `ui/discshelf/connection.[ch]`: two-profile config, source choice, backwards compatibility, session secrets and atomic private saves.
- `ui/discshelf/setup.c`: staged source chooser, source-specific editable fields, remember-password control and storage explanation.
- `ui/discshelf/smb.c`: bounded asynchronous SMB3 adapter, indexed listings, download manifest and safe file publication.
- `ui/discshelf/lifecycle.c`: exclusive worker dispatch; existing SD admission/cancellation lifecycle retained.
- `ui/discshelf/ui.c`, `landing.c`: flat SMB folder/file navigation and source-specific home label; retained menu preview, icon, smaller fonts and selected-row marquee.
- `ui/discshelf/net.c`: validated active profile and random salted Navidrome token authentication.
- `ui/Makefile`, `ui/tools/build_smb.py`, `ui/vendor/libsmb2`: reproducible static client dependency with no Kerberos, discovery or server utilities in firmware.

## Validation and limits

Tests cover private/session-only credentials, legacy config, encrypted SMB3 wire operations, paging, A–Z, recursive/Unicode/empty-file copies, safe owned replacement, unowned-file protection, cancellation after read progress, and rejection of SMB2-only and wrong-password servers. Existing Navidrome tests verify token authentication, 50,000-track local lookup, 10,003-artist paging, transfer failures/cancellation and idle worker behaviour. Real LVGL previews cover all three themes and source settings.

The local wire fixture uses upstream libsmb2's server API. Its fixture-only server supplies server-direction crypto keys and emits one directory entry per reply to avoid bugs in upstream's example server. It is **not an independent Samba/NAS interoperability test**. Samba could not run here because this environment blocks its internal Unix sockets. Hardware interoperability, peak RSS, power consumption, SD hot removal and all NAS dialect combinations remain to be tested on a Disc. Do not describe this as device-validated.
