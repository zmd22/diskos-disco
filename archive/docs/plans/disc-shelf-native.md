> Historical material. Not current 3.2.1 instructions; see the current documentation index.

# Disc Shelf: themed firmware integration plan

Status: shipped in Disco 2.7.0, including the SMB3 follow-up. Navidrome has been used on the owner’s Disc; independent SMB3 NAS/device acceptance remains pending. See docs/performance/disc-shelf-native.md for validation and remaining limits.
Baseline: public Disco 2.6.2, e8b3b3a1cf47a5e9b2fd46639c3bacd249d82213, with bundled Disc Shelf 1.0.4 corresponding source.
Scope: native Disc Shelf, compact Library-style browsing, existing selection gestures and letter seeking, downloads only. Excludes the earlier Bluetooth patch and timeout-lock proposal.

## 1. Result for the user

Disc Shelf becomes available in the firmware without installing a separate executable. Its artists, artist albums and album tracks behave like the normal Library. Keep the landing page separate, but remove the oversized app header from browsing screens. Use a compact contextual heading, the normal list start position and back-gesture clearance, and theme-specific row geometry/fonts/colours. Disc Shelf should not take a larger fixed header area than Library.

Disco, Null and Squash retain their respective appearance, dark/light or palette settings, handedness and normal navigation. Use shared firmware components rather than embedding the standalone copies of LVGL, SQLite, configuration and theme code.

Outside selection mode: tap an artist/album to browse. Swipe sideways starting in the middle of a row to select, with the existing Library gesture thresholds and edge exclusions. While selecting, taps toggle items, and the compact header shows the selection count. Back leaves selection first, and only then navigates up. The action bar contains Download only; no Play, Queue, Favourites, Playlist or playback-related More menu. Clearing all selections dismisses the action bar. Preserve the current bounded selection limit and show an explicit limit message rather than silently losing picks.

Retain selection identity across recycled row widgets and internal fetch windows using server, item kind and remote ID, not viewport row numbers. Clear selection when entering a different browse context, matching normal Library behaviour.

## 2. Radio buttons: selection and card presence

Preserve the existing local-presence radio/outline colour. Do not use selection styling to overwrite that information. Use a small SD-card outline with a check as the already-on-card icon; prefer an existing firmware glyph, otherwise a tiny vector drawing, with no bitmap/font pack.

| State | Row marker |
| --- | --- |
| Not selected; not on card | Empty radio using the normal theme outline |
| Selected; not on card | Checkmark inside the radio |
| Not selected; already on card | Existing card-presence colour retained |
| Selected; confirmed on card | SD-card-with-check inside the radio, retaining the presence colour |
| Album with some local tracks | Presence colour retained; partial-card indicator or concise “Some on card” status, never a full-completion claim |
| Availability not established | Neutral/unknown marker; no claim that the item is missing or complete |

Selection must remain visible beyond colour alone: use the centre symbol and stronger selected outline. Null gets an appropriately angular version; other themes keep their usual radio geometry.

The current local_album implementation reports local presence, not proof that every remote track exists. Preserve that distinction. Determine exact track availability and aggregate completeness from cached catalogues when available. Fetch missing detail only on opening/selecting the relevant album or downloading; do not walk every artist's albums on every list paint. Do not advertise two-way synchronization: this feature saves server originals to the card. Download skips existing files under the established duplicate checks and reports downloaded / already local / failed results. Refresh visible status after publishing files without restarting a whole-library scan.

## 3. Position dots and letter seeking

Add the same whole-list position dots/indicator as Library, correctly reflecting total list extent rather than the currently loaded 32-row window. Preserve smooth continuous scrolling; internal paging must not appear as Next/Previous pages.

Reuse azjump's A–Z grid and letter-circle mode. Follow Playback > Library seeking, including existing ring interaction, theme shapes, dismissal and timeout behaviour. Letter jumps resolve against the complete remote list, not just visible rows. Keep the existing # bucket and sorting conventions consistent with the server catalogue. Show seeking controls where the list has a meaningful alphabetic order; do not silently reorder album tracks out of disc/track order merely to provide alphabetic seeking.

The current picker implementation has a single Library picker registration. Make active ownership instance-aware before adding Disc Shelf, so its picker cannot steal or invalidate the main Library picker. Closing or leaving Disc Shelf must close its overlays and timers. Preserve Library's normal selection-mode visibility policy for the A–Z control.

Extract reusable list-position code from library.c only where necessary. Its default output and existing theme exceptions stay unchanged. Disc Shelf uses the same behaviour across the three supported themes, including Null's existing indicator treatment.

## 4. Gesture hint

On the first actionable Disc Shelf browse screen, show the themed hint: “Swipe from the middle to select”. Reuse onboarding's existing hint presentation and dismiss/discovery behaviour. Track Disc Shelf discovery separately so someone who dismissed the Library hint can still learn the download gesture. Respect the global Hints setting; do not show a popup on every open. A successful selection swipe marks the hint learned. Avoid covering the Download action or swallowing the first intended row selection.

## 5. Native integration and configuration

Import the app-specific engine, parser and network code into a namespaced shelf module; replace its standalone main/display loop with screen create/open/close hooks. Register a stable builtin app/menu identity in All apps, navigation customization and Shortcuts. Avoid changing existing saved menu IDs. Existing externally installed Disc Shelf must not produce duplicate menu entries, and its private shelf.conf and download history must be preserved.

Remove standalone config/theme stubs and connect to live firmware theme roles. Do not call lv_init/lv_deinit, replace input drivers, change global signal handlers or block the main thread while joining a worker. Audit the /proc/self/exe download helper dispatch before reuse inside mq_ui. Avoid changing process-global SQLITE_TMPDIR for the whole firmware.

Add a small server settings screen for URL, username, password and download destination using the firmware keyboard. Preserve the existing config format/location initially for compatibility, keep credentials private, and never include them in a release artifact. Do not persist passwords in the general UI settings file.

Keep streamed original-file downloads, duplicate protection, temporary files and atomic final publication. Connect operations to firmware SD admission/leases and USB-storage transitions. Exiting or cancelling stops owned network children, releases the worker and databases asynchronously, and retains completed files. A cancelled/stale response may not update a different screen or write after storage handover.

## 6. Memory, battery and size

Allocate the roughly 680 KB of existing app buffers and the 256 KB worker stack on demand. Free buffers and widgets on exit after worker cancellation completes. Retain only small registration metadata while closed. No boot-time server connection, periodic refresh, background sync, permanent worker or hidden refresh timer. No filesystem/stat/network calls from row drawing or scroll callbacks. Start initial library preparation when Disc Shelf opens, then fetch details/windows on demand.

Use the existing 32-row window and stable widget pool. The worker sleeps on its condition variable between commands. Visible progress polling must stop on close and follow the firmware's screen-off policy; downloading must count as active work for idle shutdown without overriding a deliberate shutdown request.

Planning estimate: mq_ui grows about 100–300 KB; active memory budget roughly 1–3 MB beyond the existing UI, to be measured. These are not a finished MIPS link/RSS measurement. Reuse firmware libraries and fonts; do not carry the standalone app's 3.40 MB executable into mq_ui. Measure final binary/segments and peak RSS before release.

## 7. Implementation sequence and checks

1. Introduce the native lifecycle/backend adapter and SD-safe cancellation; retain the old standalone app until the builtin works.
2. Implement compact Library-style lists and a Download-only selection adapter. Keep existing mp_host behaviour as the default for Library/Folders.
3. Add position indicators, instance-safe A–Z modes, hints and card/selection marks.
4. Apply theme-specific layout, server setup and existing-install migration; register builtin menu/shortcut entries without duplicates.
5. Validate and measure, then remove the separate executable from new release packaging while retaining source/license obligations and user configuration.

Required checks:
- Real LVGL renders for each theme: artists/albums/tracks, long header, selected rows, mixed card availability, grid and circle picker, download/errors and setup keyboard.
- Swipe selection versus vertical scrolling, edge Back, long press and letter seeking; both handedness modes where supported.
- Selection persists through row recycling and distant window/letter jumps; no selections leak into another server/list.
- Large catalogue/duplicate album names, punctuation and non-Latin names; mixed complete/partial albums and missing files.
- Downloads, cancellation, close/reopen, connection failure, full card, missing card and USB storage transitions; completed files remain intact.
- Main Library/Folders selection actions and both letter pickers remain unchanged after entering/exiting Disc Shelf.
- Repeated open/close returns memory near baseline and leaves no live worker, poll timer, network child or open SD lease. Closed Disc Shelf produces no ongoing work.
- Playback, Bluetooth/USB modes, screen-off and shutdown remain safe during browsing/downloads.
- Provide matching source changes, measured binary size/RAM results and a test mq_ui only after the implementation and review stages.
