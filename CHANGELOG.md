# Changelog

All notable changes to diskOS Disco! (and the diskOS releases underneath) are documented here.

Entries follow the Keep a Changelog format, with Added, Changed, and Fixed categories where applicable.
diskOS remains beta software; version numbers do not imply broad hardware or feature validation.


## 3.3.0

Braun 2.0, a new light and dark theme, plus fixes it brought out. [Release notes](docs/releases/v3.3.0.md)

### Added
- Braun 2.0 theme (Display > Theme): warm ivory/charcoal panels, a speaker-grille texture, shaded keys with orange icons and one orange accent, Inter type that follows Font Size.
- Braun 2.0 Home: a wall clock with a date window, weather and Wi-Fi/Bluetooth/battery glyphs on the face; Library, play/pause and Search below.
- Braun 2.0 Music: artwork and wrapping title/artist/album over a radio-dial seek scale with scrubbing preview; Layout (Art left with a straight scale, or Art on top with a circular scale), Controls (Buttons or a gesture zone), favourite and play mode beside the time, a tap on the title for the track menu and a hold for Immersive.
- Braun 2.0 Quick Settings: eight keys, battery and brightness tick scales; the QS shade, Working mode, Shortcuts (six slots) and Weather (six forecast keys and a now panel) in the same keys.
- Braun 2.0 volume: Braun's right-rim dial with a LO/HI gain switch and LED (firmware with gain support).
- Braun 2.0 EQ: Braun sliders with orange travel; track/album/file menus as squircle keys with the full song set of the other modern themes.

### Fixed
- The 44 px time font had no colon (Braun and Braun 2.0 showed a 20 px colon); the A-Z wheel's large letter fell back to a small font in Braun.

## 3.2.2

A default cover for songs without one, wheel lists that open with their buttons in view, quoted tags on Now Playing and a Queue that always starts and comes back. [Release notes](docs/releases/v3.2.2.md).

### Added
- Default artwork: a song with no cover anywhere (embedded, cover file or Online Album Art) shows a grey note on dark grey on the Music screen, backdrop, thumbnails and Immersive instead of an empty square. It never sets the accent colour and is never cached as the song's cover; Online Album Art still looks for a real one.

### Changed
- Wheel lists: an album, and an artist's albums, open with the cover / Play / Shuffle row just under the screen header and the first rows visible below it; rows never overlap that taller row while scrolling.

### Fixed
- Queue: a queued song always starts while something plays. When the playing song or its list can't be read, the queue plays on its own and then returns to the album or playlist it interrupted, after the interrupted song (it used to refuse with "Couldn't start queued song", and later to stop at the end of the queue).
- Now Playing stayed on the previous song while the next one played when a tag (title, artist, album, path) held a double quote, e.g. an album tagged `"Let's Rock"`: the stock player doesn't escape quotes in its track message. Disco now rebuilds such a message from its fixed layout and reads it, cover included.
- Wheel lists in Squash showed the row pills until the first scroll.
- Zune (pull request #22): search result text, empty rows while scrolling the theme list, and duplicate Weather/Equalizer app shortcuts (now Album Roulette and Boomshine).

## 3.2.1

Smoother wheel lists, depth for Disco's buttons, idle power-off that respects the charger, a safer power-button shutdown and a Queue fix. [Release notes](docs/releases/v3.2.1.md).

### Changed
- Wheel lists: size and colour change smoothly with the distance from the middle; rows pack round the large middle row and follow the round edge; the Library's main lists show their size ("80 songs") above the first row.
- Disco: buttons have a little depth (lighter top, fine light edge, soft shadow), flat when pressed.

### Fixed
- Wheel mode: the Queue (and lists below a status, like Bluetooth and Wi-Fi) no longer move over their buttons; the buttons respond again and "Nothing queued" shows.
- Idle power-off never switches the Disc off on the charger, even with a full battery or one held at 80 % (the charger is remembered until the level drops; a computer USB connection counts).
- Holding the power button: stats and battery history are saved and the card flushed before the player's own power-off.

## 3.2.0

Wheel lists, the QS style *Shade*, the letter wheel and A–Z keypad, round track menus, one header line everywhere, cleaner fades, idle power-off fixed and a battery-friendlier interface. [Release notes](docs/releases/v3.2.0.md).

### Added
- Wheel list motion (Disco, Squash, Null): the current row pinned to the middle of the screen, one row above, smaller and greyer rows below under a soft fade; snapping.
- Display › QS style: *Classic* (as before) or *Shade* - a now-playing shade first (cover tap = play/pause, previous/next, title/artist tap = album, progress, clock and battery), a second pull for the theme's Quick settings; swipe up closes, the Home swipe works.
- Letter wheel: a rim index along the right edge (left-handed Disco: left), turned clockwise/counter-clockwise, the list following live; Zune uses it too. Holding the rim scrollbar opens the letter picker.
- A–Z keypad: round keys; empty letters dimmed and untappable, the current letter filled.
- Round track menus (Music pop-up and Library hold-a-song): cover and back arrow in the centre; Favourite, Lyrics, Album, Artist, View queue, Info, +Queue, +Playlist.
- Song info: Auto-tag (lyrics and artwork the song is missing).

### Changed
- One header line on every screen (Disco, Squash, Null): title centred, back arrow beside it, titles that always fit.
- Play / Shuffle pills the same everywhere; wheel album row with cover, pills, album and artist; album title under its cover; small covers in an artist's album list.
- List fades only in empty space; pre-drawn, dithered fades; exact, rounded colour blending.
- Queue, Equalizer, Search and volume pop-up tidied; pressed-state and pop-in motion; *Back* instead of *Cancel* in round menus.
- Squash screensaver: larger clock, darker picture.

### Fixed
- Idle power-off: decided by a still song position (the sound chip never goes quiet while paused).
- Text never scrolls vertically (all themes).
- Disco: the large wheel row stays clear of the navigation shard.
- Battery: the interface sleeps between events; idle polls slow down; 15 fps vinyl screensaver; weather backoff without Wi-Fi.

## 3.1.0

Automatic appearance, menu-edge fades, medium-weight Zune menu fonts, idle timer and queue/history persistence fixes. [Release notes](docs/releases/v3.1.0.md). Device testing pending.

## 3.0.1

- Replace the A–Z wheel with Focus: the selected letter stays at twelve o’clock, neighbouring letters fade with distance, and subtle accent highlights soften the ring. Use each theme’s fonts and colours; omit the heading above the centre letter.
- Unify Playback, Audio, Display, Connectivity, System, Battery & power and Help across themes. Separate theme-owned controls from common settings; put power timers, charging protection and battery Details together.
- Remove the old Equalizer shortcut, rename Custom EQ to Equalizer, move read-only DSD information to Audio, and remove the unused startup disc-colour setting.
- Preserve saved setting keys, values and handlers, with explicit anchors followed by alphabetical rows and On/Off controls last.


- Fix idle shutdown remaining latched after asynchronous card-drain, flush or command failures; retry after a fresh full idle period.
- Cancel pending idle shutdown on renewed activity before the player handoff, recheck audio before handoff, and invalidate timer takeover immediately on player reconnect. Explicit manual/sleep shutdown and protected-card handling remain separate.
- Warn when stock idle-timer takeover stays unconfirmed, log shutdown outcomes, and clarify external-power/working-mode/Disc Shelf exclusions in settings.

- Preserve theme visuals and interactions while caching shared list-circle calculations and Zune font fitting.
- Avoid redundant Null menu colour writes while retaining its existing motion and timing.
- Safely retire Null menu timers on parent teardown and handle timer-allocation failure.
- Add cross-theme animated/static render comparisons and repeated menu-lifecycle checks; refresh stability-test stubs for the current interfaces.

## 3.0.0

Zune theme by [namastecoconut-imagined](https://github.com/namastecoconut-imagined/diskos-disco-zune), with Disco integration and legibility polish by zmd22. [Release notes](docs/releases/v3.0.0.md) and [validation checklist](docs/RELEASE_3.0.0.md).

- Website: Zune-first hero, detailed acknowledgements and an interactive seven-screen/four-accent walkthrough using real LVGL renders.
- **Immersive 3.0:** shared transport overlay and spinning-disc entry/exit in Zune.


- Zune main shortcut: use a fixed four-square outline for every action, with the upper-right outline in the current accent. Draw the icon as geometry to eliminate missing-font boxes; retain tap/double-tap/hold and edge positioning.

- Zune USB-C audio output: add a distinct output tile, pause and prove quiet before host switching, wait for a stable USB playback device, rebind the real sink, then resume only the same track/session.
- USB recovery: restore headphones on enumeration timeout; retry busy/blocked unplug fallback without losing it; retain recovery state after failed restoration. Block source/Bluetooth routing around an active USB route until Playback restores headphones. Added production-state-machine failure tests; real DAC compatibility still needs device validation.

- Zune Home: enlarge the Now Playing caption from 14 to 16 px with a wider, padded line box while preserving 64 px menu spacing. Home focuses Now Playing; bottom-edge Home swipes also work from Music and take priority over list-rim scrolling.

- Zune volume A: thin accent rim, oversized number, quiet gain label and square accent marker; 100 ms entry fade with direct touch adjustment and modal seek protection.
- Verify Zune spinning-disc Immersive entry/exit for Vinyl, CD and Cover, including early exit/reopen and Animations off.

- Fix vertical text marquees in Listening Stats, SongSphere headings/moods/tag details, and Tune values: single-line labels now size themselves to the final theme font. Horizontal overflow scrolling remains available.

- Zune: separate confirmation headings, readable messages and buttons; fix SD update and reset dialog overlap.
- Zune: center the edge shortcut vertically and configure its tap, double-tap and hold in Display > Music screen > Zune; hide the unused Now Playing style option.
- Volume: prevent Music seek/navigation underneath the overlay; Zune consumes taps outside the dial. Keep the overlay open while dragging.
- Zune: reuse the flying-disc Immersive entry/exit animation and return to Music.

- Zune folders: stop vertical title animation, fit a full text line to each curved row, and scroll only the focused long name horizontally. Covered folder pages stop scrolling.
- Zune Music: move artwork 36 px farther left, widen the metadata column, and show format/bitrate/sample rate below duration using the shared Immersive cache.

- Zune polish: keep paused QS text centered; loop title/artist continuously at about 56 px/s, including short metadata. Repeated playback updates preserve scroll progress; hidden/panel-off headers stop animating.
- Zune typography: Music title 24 → 22 px and artist 16 → 17 px; larger settings values, battery details, folder/book metadata and technical information. Bound secondary headings to the round screen and reflow Debug Access, Last.fm, How To and Up Next.
- Zune library: Small/Medium/Large now select distinct list fonts, with the existing layout retained at Medium and more row height at Large.

- Zune debug pass: avoid repeated metadata/QS/standby writes; stop hidden loading arcs and metadata marquees; preserve seek-rim gestures beside the wider text area.
- Immersive: cached file type, bitrate and sample rate in every theme; correct long-name auto-hide in Null.

- Zune branch: reuse Squash's search magnifier; wider two-line Music metadata, accent transport and bolder small menu text; correct idle-to-playback metadata restoration.
- Null Immersive: start the renderer independently of Music layout, retain sharp Cover/Vinyl/CD artwork, and use smaller two-line control metadata without scrolling.
- Immersive: clear previous-track progress when duration becomes unknown.

- Zune branch: cancellable immersive edge Back with scrubbing enabled or disabled; six configurable themed Quick Settings slots retaining the original actions and shared radio/EQ behavior.
- All themes: text-only List, three-column Wall and Zune-style Coverflow album views; header category swipes and adjacent-category taps. Albums shortcuts respect the chosen view.

- Development preview: optional Zune theme, adapted from namastecoconut-imagined’s theme kit, retaining 2.7.0 transport, radios, multiselect and download handling. See [integration notes](docs/ZUNE.md).
- Disc Shelf music overlay: separate scrolling title/artist, position bar, higher heading and smaller lower Close button.

- Disc Shelf: small D! button on the download screen opens a themed music overlay with scrolling Artist - Title and previous/play-pause/next controls. Uses existing transport handling and the visible-app refresh cycle, without a new worker or timer.
- Home navigation, Back and the header Back control cannot leave or cancel an active copy. Use Cancel, then wait for the worker to finish cleanup before leaving. Back can dismiss the music overlay while retaining the copy.
- Null: hide the global menu drawer in Disc Shelf so it cannot cover the music button; preserve Disc Shelf's own themed layout when entering or returning from Quick Settings.

## [2.7.0] - 2026-10-22

### Added
- Native Disc Shelf, with exclusive Navidrome/SMB3 source selection and separate profiles.
- Themed Library-style rows, swipe selection with Download-only actions, pagination/position dots, A–Z grid and letter wheel, and a first-use gesture hint.
- Recursive SMB3 file/folder downloads, including partial collections, Unicode names and empty files/folders. SMB3 encryption is required; SMB1/2 are not used.
- Missing-only downloads and explicit replacement of copies owned by the same source. Unrelated local files remain protected.
- Session-only passwords by default, optional remembered credentials, native masked input and private configuration writes.
- Themed guard screens for Disc Shelf and SongSphere; browsing past them does not initialize their apps.
- Shuffle! menu action in Disco, Null and Squash: use Library’s all-songs shuffle, return to Music and show the themed Happy listening! popup.

### Changed
- Navidrome download layout: Artist/YYYY - Album/NN. Track name.ext. Album years are retained from existing responses; missing years use 0000. Real collisions get readable suffixes; existing manifest paths and SMB filenames remain unchanged.
- Disc Shelf’s own CD-in-jewel-case icon, wider rows, aligned headers, smaller list type and selected-name scrolling.
- Squash Settings headings align with the back arrow; the arrow is slightly further inward.
- Download status is limited to three lines with ellipsis, with a smaller percentage, smoothed speed and a larger MiB counter.
- Disc Shelf temporarily disables Wi-Fi power saving during Navidrome/SMB3 copy jobs, restoring the previous mode after completion, cancellation or errors. An already-disabled mode is left alone; there is no background polling or persistent setting change.
- Navidrome coalesces small pipe packets into 128 KiB SD writes. A localhost test made about 75% fewer file-write calls; this is not a measured on-device speed gain.
- Navidrome authentication uses a random salted token. Tokens remain reusable credentials; HTTP and helper process arguments are not private.
- Updated website, how-to, setup, tour, privacy and release documentation. Legacy standalone Disc Shelf remains a separate older-firmware download.

### Validation
- Navidrome works on the owner’s Disc. Host HTTP/SQLite, transfer-fault, native lifecycle, source/credential and themed LVGL checks cover the new paths.
- SMB3’s encrypted wire fixture passed, but independent physical NAS compatibility, device power/peak memory and hot-card removal remain unverified. SMB3 is experimental.
- No Bluetooth or upstream-settings migration changes relative to public 2.6.2. Existing menu action numbers remain compatible.

[2.7.0 release notes](docs/releases/v2.7.0.md)

## [2.6.2] - 2026-10-21

- Bluetooth: headphones known to support the chosen codec connect straight on AAC or LDAC (BlueALSA started preferring
  it), so there's no switch and no pause; unknown headphones work as before and are learned; other headphones get the
  normal order. Tested by hand on a Disc; the automatic version not yet.
- The Connected pop-up names the headphones and the codec actually running (also on automatic reconnects).
- The Bluetooth screen updates when headphones connect or disconnect while it's open.
- Bluetooth screen polish: one "Connected to <headphones> - <codec>" line, bigger rows, larger hint text, less space
  at the top.
- Clock field on Disco, Squash and Null a touch larger; new Clock and info page with Battery percentage and Weather
  (weather off: the clock isn't a button); Null's clock fades with the weather when the title lifts.
- Back-swipe arrow on the track pop-up; it closes on release, like Back everywhere else.
- Ring and Braun: development frozen (fixes only).
- Headphones are marked with their known-good codecs; switched off, the next connection is prepared to start on the
  right codec; a quick off/on is switched up again instead of staying on SBC.

[2.6.2 release notes](docs/releases/v2.6.2.md)

## [2.6.1] - 2026-10-21

- Bluetooth: headphones no longer drop mid-song. A late answer from the link check (busy Disc, screen off) is no
  longer taken for a lost connection; only three real "no link" answers in a row count (2.6.0 re-routed, which
  restarts the player's Bluetooth service). Music started right after connecting waits for the AAC/LDAC switch
  and starts on it, instead of pausing a few seconds in. A skip during the short codec-switch pause no longer leaves the music
  paused on the new song.
- Nothing is stopped or powered off because Disco merely didn't see music playing: the audio system is asked too.
- Idle power-off no longer switches the Disc off in Bluetooth DAC or any other working mode, or while plugged in or
  charging (#17).

[2.6.1 release notes](docs/releases/v2.6.1.md)

## [2.6.0] - 2026-10-21

- Fast skipping: on-screen skips paced (one per 0.3 s, up to 8 waiting); covers decoded once a song has been current
  0.3 s; play history counted after 1 s.
- Bluetooth: no automatic play/pause toggles (they could pause working audio a few seconds in); AAC/LDAC switches never
  hit a playing song (pause, switch, confirmed by BlueALSA, play); codec fallback LDAC ->
  AAC -> SBC; reliable re-routing (same headphones, source changes, songs still starting, failed steps reported);
  search stopped before connecting, Disconnect cancels a pending connection, fast off/on, truthful Forget; stale
  commands dropped after a player restart; connected device found in long lists. AAC/LDAC untested on a Disc.
- Help: hints and How-to up to date; new pages Queue / picking and Start-up / battery.
- Crash log at /usr/data/diskos_crash.log (starts, player restarts, crashes, low memory, stalls).

[2.6.0 release notes](docs/releases/v2.6.0.md)

## [2.5.9] - 2026-10-20

- Windows app: on every start it checks for a newer release and, if found, updates the installer files inside its
  environment (installer, boot scripts, interface; checked against the release's checksums, nothing changed on a
  mismatch). An environment set up by an older app no longer keeps installing that app's version.

From the 2.5.8 memory and CPU audit:
- Queue: a manual skip with nothing queued no longer reads and maps the player's whole list; the song-path index is
  ensured when the library opens (not only on a rescan).
- Bulk Queue / Add folder to queue: one 2.5 s time budget for the whole action; a locked database gives up after one
  wait with "Library busy - try again" and adds nothing (the selection stays), instead of waiting 1.5 s per song.
- Playlists: only the rows that can be shown (8192) are loaded, and the song list is released once the screen is
  fully hidden.
- Most / Recently Played reserve room for the history, not the whole library; the buffer is released in other views.
- Hidden library lists stop scrolling their focused row (it kept the main loop in its busy pace).
- Immersive: the CD sheen buffers (0.74 MB) are released with the rest when Immersive closes.
- The player's list is mapped to songs in one query instead of one per row; the path index is built in the
  background 45 s after start-up instead of when the library opens.

[2.5.9 release notes](docs/releases/v2.5.9.md)

 - 2026-10-19

- Bluetooth (#13): with LDAC/AAC, music waits for the codec switch before playing instead of stopping ~6 s in; no
  device search during Bluetooth playback (it caused short dropouts). Not yet tested on a Disc with LDAC/AAC.
- No back-swipe arrow on Home, where Back leads nowhere (all themes).

[2.5.8 release notes](docs/releases/v2.5.8.md)

## [2.5.7] - 2026-10-19

- Bluetooth: the resume guard is bound to the same headphones, player session and song; one play after a re-route,
  otherwise a hint; the 2.5.6 codec blacklist is removed (#13).
- Code-review fixes: exact songs for every multi-select action (CUE/ISO tracks, same-named albums, folders, no
  512-song cap), truthful bulk Favourites, a failed queue start keeps the queue, Files Select all, faster bulk Queue.
- Windows: the installer downloads the latest release each time (or uses a chosen mq_ui); "unsupported, but working
  as intended" wording. Docs: 2.5.5's "untested" notes removed.

[2.5.7 release notes](docs/releases/v2.5.7.md)

## [2.5.6] - 2026-10-19

- New: pick several songs, albums, artists or folders (swipe across a row); Play, Queue, Shuffle, Add to playlist,
  Add to Favourites. First-use tip and a How-to line.
- Queue: queued songs play again when it's their turn (regression since 2.5.1); with nothing playing, Play/Next
  start the queue (shuffled in Shuffle mode).
- Bluetooth: songs no longer stop seconds after a playlist starts (#13, resume guard); headphones that can't hold
  LDAC/AAC fall back and are remembered; stop searching before connecting, no re-pairing, reconnect the last
  headphones, no stutter while connecting. Bluetooth not fully tested on a Disc; AAC/LDAC not at all.
- Squash: list rows at the curved edges no longer show as selected while scrolling.
- Docs and Windows app: switching between diskOS builds, start from stock V2.57, unsupported Windows notes.

[2.5.6 release notes](docs/releases/v2.5.6.md)

## [2.5.5] - 2026-10-19

- Experimental: Thai (tone marks placed above vowels), Vietnamese and Korean (2,350 syllables) in titles, artists,
  albums and lyrics.
- Covers are cached until less than 500 MB is free on the card (was 1 GB).
- Battery: hold the "Since full charge" line to restart the totals.
- Docs and website: how-to matches the Display menu, showcase video, install advice.

[2.5.5 release notes](docs/releases/v2.5.5.md)

## [2.5.4] - 2026-10-18

- Shared Display settings organization, explanations for unsupported controls, static wrapping setting labels and reduced invisible/repeated visual work.
- Larger Null list/Music fonts and hold-to-scrub time preview with guarded seek-on-release.
- Optional Immersive circular duration/volume control, finger-driven artwork rotation, compact time feedback and left-edge Back gesture.
- Cover replaces None and rotates without disc decoration; lyric changes slide using cached snapshots.
- Squash light/dark weather with four paired forecast pills and a bottom current-weather flyout; centered Squash/Null Battery content and downward arc opening.
- Shorter Roulette, Boomshine and Stats landing copy. All dev1–dev3 patches included; existing 2.5.3 features retained.
- Focused native/build checks passed. New controls still need extended device testing; no measured battery-life gain claimed.

[2.5.4 release notes](docs/releases/v2.5.4.md)

## [2.5.3] - 2026-10-18

- Null Matrix palette, fixed palette accents and less repeated UI styling.
- Matching fixed Shortcuts/Working mode layouts in Null and Squash; Shortcuts swipe feedback in all themes.
- History tracking while screen is off and guarded History row interactions.
- Native validation only; physical testing pending. No experimental migration feature.

## [2.5.2] - 2026-10-18

- Fresh-install defaults: Disco dark, hints On, linear progress, no Music track-number prefix; saved choices retained.
- Roulette result actions beside Play, larger two-line metadata, 224px stored covers, direct capped built-in decoding and safe release of hidden artwork.
- Squash Working mode and Shortcuts share fixed pill rows and larger headers, with all five modes visible without scrolling; clearer Wi-Fi/BT controls, names/status and centred larger logos; disabled shared Accent Colour with explanation.
- Popup horizontal metadata scrolling and white unselected favourite icon; opaque A–Z letter-ring backing.
- macOS installation guide using upstream 1.2.0 Apple Silicon tools, plus the Intel build route.
- Orange/black website with the firmware logo font, restrained rainbow accents, pinned desktop chapters.
- Native UI, artwork/decoder and regression checks passed; device validation pending.

## [2.5.1] - 2026-10-18

- Squash volume and Library highlight fixes; bounded Album Wall background work, deferred prewarm, nonblocking database reads and lazy cold buffers.
- Lossless Roulette resource compression; themed Again/Exit actions.
- Continuous track-popup metadata; optional Library letter wheel with a 50% background and one-second release deadline.
- Bounded previous log copies, version identification and rate-limited slow/failure diagnostics.
- Test scope and limitations: `docs/performance/2.5.1-test-notes.md`.

- Removed: hidden legacy theme registrations, component kits, palettes and 39 unshared font sources. Preserve visible Stone, Ring, Braun, Disco, Null and Squash at their original IDs; retired IDs migrate to Disco. Braun's shared 24 px title face remains.
- Validation: 36 Ring/Braun render comparisons are byte-identical across light/dark Home, NP, QS, Library, Settings, EQ, Search, Queue and standby; active/retired ID migration and Stone palette preservation pass.

- Changed: QS EQ tap toggles the remembered preset with an On/Off + preset popup; Hold opens Equalizer in all themes. Active styling follows the selected state, including renamed user presets.
- Fixed: centred Squash overflow metadata returns to its beginning after scrolling; new short metadata restores configured alignment.
- Performance: Squash reuses valid local artwork across folders for matching album/artist; faster fixed-ratio blur upscaling retains exact previous output bytes and compacts conversion in place.
- Polish: subtle D! shading in both Squash appearances; long launcher names wrap above the hub with cached font fitting.

- Fixed: Squash volume final-write retries and delayed player-level reconciliation, slow-swipe versus Title Hold handling, idle Go to Library navigation and unknown QS battery readings.
- Changed: Squash QS artwork returns to Music, with a separate 40 px play/pause control; battery increases from left (0%) to right (100%).
- Performance: retain unchanged Music status labels, remove hidden launcher counter updates, use an interpolated integer orbit table and adapt refresh scheduling between touch/animation and rest.
- Validation: focused native regression checks pass; physical-device validation remains outstanding.

## [2.5.0] - 2026-10-17

- Added: **Squash**, a bold Nunito-first theme with fixed 36/32/28 px Music text, touch playback, rising circular launcher and eight icon Quick Settings. Original and Inter keep the same sizing rules.
- Added: Squash’s bottom Volume B arc with D! dismissal, undimmed background, drag-aware auto-hide and throttled final commits.
- Changed: website leads with Squash; customizer, Null and existing links are retained. Help, How to and contextual hints receive prominent guidance and current Squash instructions.
- Fixed: metadata auto-shrinking, duplicate track-time shadows, header-arrow centring and configured Hold actions on Squash launcher icons. Status icons have independent toggles beside D!; blurred backgrounds use a 20% overlay.
- Retained: cumulative Null/theme/editor/help improvements since v2.3.0 and existing theme-specific settings and behavior.

[2.5.0 release notes](docs/releases/v2.5.0.md) · [Squash guide](docs/themes/squash.md) · [release handoff](docs/RELEASE_2.5.0.md)


## [2.4.0] - 2026-10-16

- Null theme: pixel typography, palettes, black/darkened/pixelated artwork backgrounds, sliding navigation and a
  vertical Quick Settings panel; text or icons and four Music font combinations.
- Quick Settings items can be added, removed and reordered separately in each theme.
- Clearer settings: larger centred selectors and explanations, better touch targets.
- Help with How to pages, contextual gesture hints and separate first-use hint preferences per theme.
- Calmer scrolling: only the focused library row scrolls; Music text scrolls three times after a track change.
- More readable Disco standby; themed Bluetooth, Wi-Fi and track information screens.
- themes.md: the rules for adding a theme without changing the existing ones.

[2.4.0 release notes](docs/releases/v2.4.0.md)

## [2.3.0] - 2026-10-15

SD update from 1.4.1 or later (no reflash).

- Added: Immersive (Disco): Music's pieces step aside and the cover drops onto the record, spinning in and slowing to
  its pace; closing lifts it off and Music slides back. Only with Settings > Animations on.
- Added: Disco Options > Music Screen > Controls: Buttons, Auto-hide (buttons and play mode fade after 5 s, a touch
  brings them back) or Gestures (double tap play/pause, swipe right next, left previous, with faint hints).
- Added: hold the progress line or arc to scrub, with the time shown large.
- Added: Listening Stats Highlights: longest streak, busiest day and average listening day.
- Changed: Listening Stats: a straight Week / Month / Year pill, a bigger period name with < > arrows, "2h 40m of music"
  on one line, unboxed counts with fading dividers, and larger text throughout.
- Changed: Quick Settings Arc: seven 76 px buttons along the screen's curve, Wi-Fi in the middle, Bluetooth first and
  Settings last; List keeps Wi-Fi and Bluetooth on top and Settings at the bottom. Seven items at most (an older list
  drops its last other item; the default drops Rescan). Sun and battery icons centred in their gaps.
- Changed: Battery: the clock time it lasts until, time to full while charging, a thinner ring with a glowing tip and
  tick marks, and two plain figures instead of pills.
- Changed: Library (Disco): rows reach further into the free edge, and titles run up to the count or duration.
- Changed: Search shows up to five artists, the ones with the most songs first.
- Changed: the website: new hero, Form and Function chapters, new screenshots.
- Fixed: Battery: the playing / screen-on totals never restarted after a charge made with the screen off, and
  screen-off minutes were logged at a stale level; the page now reads the gauge itself once a minute.

## [2.2.2] - 2026-10-15

SD update from 1.4.1 or later (no reflash).

- Added: A surprise!
- Fixed: Stats: album covers in the Wrapped bubbles and the #1 now come from any track of the album the art cache has
  (else the small thumb), each bubble shows its own letter, and the letter draws in any script.
- Changed: Stats: < > arrows at the ends of the Week / Month / Year arc show it can be swiped.

## [2.2.1] - 2026-10-14

SD update from 1.4.1 or later (no reflash). Fixes only.

- Fixed: USB DAC was silent on the latest firmware: the player now gets its own USB DAC work mode (1), not local
  playback (8).
- Fixed: Listening Stats added up restarts of the same song: two 20-second tries counted as one 40-second play, and
  short Repeat One loops merged. Every jump back to the start of a song is now a new attempt that counts on its own.
- Fixed: SongSphere's move of the separate app's placements could leave them looking lost (a failed copy or rename
  left an empty new file and was never tried again). It now uses SQLite's backup, checks the copy, and until it
  succeeds SongSphere keeps using the old file and tries again on the next start.
- Fixed: SongSphere could freeze while picking songs on very large libraries with thousands of albums (a full
  counting table). Only picked songs are counted now, and the lookup is bounded.
- Fixed: Album Roulette's loading thread is collected once it finishes after you leave, instead of on the next visit.
- Fixed: app icons: the BMP compression field is read in full; bitfield BMPs (whose channel masks aren't read) are
  no longer shown wrongly but fall back to the letter tile.


SD update from 1.4.1 or later (no reflash).

- Added: **SongSphere is built in** (Settings > Playback > SongSphere, on by default). Place albums, artists and now
  single songs: a **Mood** action in the Library's album and artist hold menus, and a mood pill in Music's track popup
  (the song's mood in its colour, or Set mood). Play a mood from SongSphere's own screen (Disco Menu, Apps, All apps).
  Placements live in `/usr/data/songsphere.db`; the 2.1 add-on's placements are taken over the first time. A separate
  install in `/usr/data/apps/songsphere` is no longer used and can be removed.
- Added: **Album Roulette is built in** (it was a separate app): I feel lucky! on its vinyl page shows the
  roulette wheel while albums are prepared, then the spin starts by itself; Play! or the cover plays the album in
  track order; hold the cover for another spin. It's in Apps, All apps and the Disco Menu without installing
  anything, and reuses the old app's cover cache. A separate install in `/usr/data/apps/album-roulette` is no longer
  used and can be removed.
- Added: **Apps**, a Disco Menu section: up to five apps you choose (Choose apps), each with its icon, opening its
  landing page. Apps can ship an icon (`icon=` in `app.conf`, else `icon.bmp`).
- Changed: **Listening Stats**: Week / Month / Year on an arc and the period as a strip you swipe (or tap a
  neighbour); the overview counts songs, albums and artists. Top Artists, Top Albums and On repeat show their top 5;
  tap a section's title for its top 15, or the #1 cover for its Wrapped (the covers as bubbles, then the top 5 with
  their colour and time).
- Docs: tour (Listening Stats, SongSphere, Apps), SongSphere page, website, README.

## [2.1.0] - 2026-10-13

SD update from 1.4.1 or later (no reflash). SongSphere is a separate app, installed over SSH.

- Added: Listening Stats, built in (landing page; Disco Menu "Stats", All apps, Shortcuts). Week, Month or Year with
  back arrows; listening time and how it compares (a running period with the same stretch of the one before); plays,
  songs and best day streak; top artists, top albums, on repeat and top songs (tap to play); a 24-hour listening clock
  round the rim. One list that curves like the Library. A play counts after 30 s (or almost all of a short song); time
  is how far the song really played (pause and seeks don't count; music during an app does); audiobooks are left out.
  New diskOS table PLAY_LOG in the library database; stats start with this version.
- Added: SongSphere, a separate app (`apps/songsphere`): place albums and artists on a mood sphere (Energetic/Calm,
  Dark/Bright) by dragging a marker, then pick a mood and play up to 40 nearby songs in Shuffle, optionally kept as a
  playlist. Its own small database in its folder; the library is only read.
- Added: app hand-over for installed apps: mq_ui writes `/tmp/diskos-app-env` (theme colours, light/dark, rotation)
  before an app starts and plays or saves the songs an app leaves in `/tmp/diskos-app-request` when it exits.
- Added: Display > Text & Layout > **Immersive** (Vinyl, CD or None: the plain cover, standing still) and
  **Immersive Lyrics** (on/off: lyrics and their shade), in every theme. Replaces Disco Options > Immersive (a CD
  choice carries over).
- Added: first-use tip (Disco) "the power button locks the screen in a pocket", once, after a touch wakes a timed-out
  screen. The Music tip now also says that holding the title opens the full-screen cover.
- Fixed: a touch on a dark screen (timed out or dimmed) pressed what was under it: now it only wakes the screen and
  the buttons listen again once the finger is lifted. Locking stays with the power button.
- Fixed: By Artist, Play All/Shuffle and tapping a song played the player's exact-tag list (collaborations and
  spelling variants left out; a collaboration continued through the whole library). Now exactly what the list shows
  plays, as By Album Artist already did. An artist's All Songs row in Album Artist mode used the wrong name.
- Changed: album rows (Library, Album Wall) show the album artist, or Various Artists for mixed albums.
- Changed: installed apps open their landing page from All apps and Shortcuts too; All apps hides folders without a
  working app and lists up to 38 apps.
- Fixed: first-use hint boxes size to their text (the Shortcuts hint was cut off; the Library hint overflowed at Large).
- Fixed: signing twice within a minute: `diskos-sign.sh` keeps the last update number with the keys and always uses a
  later one; without a version it labels the update `custom-<number>`.
- Docs: tour (Listening Stats, Standby locking, Immersive settings), how-to, SongSphere install guide, website.

## [2.0.0] - 2026-10-13

SD update from 1.4.1 or later; the Windows app can also flash.

- Added: diskOS Disco! for Windows (`windows/`): a .NET Framework app (one small EXE) with four pages. Check (Windows 11
  x64, virtualization, WSL, usbipd-win, space, pending restart, each with a fix button); Prepare (imports the app's own
  WSL distribution `diskOS-Disco` from `diskos-wsl.tar.gz`, built by `windows/wsl/build-env.sh`: minimal Ubuntu 24.04,
  installer, Python deps, static usbboot, kmod, signing tools; runs as root, so no udev rule); Install (firmware, new or
  existing keys, usbipd bind/attach --auto-attach, `diskos install --yes`, keep-awake, stock backup copied to
  `%LOCALAPPDATA%\diskOS-Disco\backup`); Update (latest release download checked against SHA256SUMS, `diskos-sign.sh`,
  written to the SD card). Start-up disclaimer, About with thanks, Copy report.
- Release workflow builds the Windows app and its environment; new asset `diskOS-Disco-for-Windows.zip` (fixed name, so
  `releases/latest/download/` links always work).
- Fixed: false "Couldn't start playback" after starting a big list: the start timeout is 12 s and only toasts if
  nothing plays.
- Docs: WINDOWS.md rewritten as the app's guide; README, website (new hero and features, Windows section), install
  pages and tour point Windows users to the app.

## [1.5.0] - 2026-10-13

SD update from 1.4.1 (no reflash).

- Added: Boomshine 1.2.0, a chain-reaction game, built into `mq_ui` (`--boomshine`, launched like MA Sendspin's helper).
  In the Disco Menu, All apps and Shortcuts; scores in /usr/data/boomshine (copied once from the old app folder).
- Added: installed apps in the Disco Menu (`disco_mnu` digits mapped to app folders via `dm_app0..9`), stable across
  installs and removals. Folders without an executable `app`, and old `album-roulette`/`boomshine` folders, are skipped.
- Added: app landing page for Boomshine and installed apps; `app.conf` may set `tagline=`, `about=` (`\n` for a second
  line) and `button=`. Choosing an app in the circle shows its page; only its button launches.
- Disc Shelf: `app.conf` with landing text (always refreshed on install); install guide rewritten with more detail.
- Docs: Boomshine and Apps in the menu in the tour; install guides use the address from Debug Mode.

## [1.4.1] - 2026-10-12

**Needs a reflash** (boot-script fixes). Bug reports by [ingvarr777](https://github.com/ingvarr777), issues #1-#8: thank you.

- Security: MA Sendspin WebSocket frames with a top-bit-set 64-bit length bypassed the bounds checks (heap overflow in the
  root helper, reachable from the LAN). Lengths are unsigned and capped at 16 MB; buffer growth is checked
  (private report by ingvarr777).
- Fixed: SD updates after the first one rolled back (the boot record outgrew the UI's 256-byte read) (#8).
- Fixed (boot script): first SD update after a fresh flash dropped; a trial whose boot count can't be saved now rolls
  back; a build that can't be checked for a moment is kept, not deleted; capped install log; a stale rejected copy
  no longer blocks reinstalling (#7).
- Re-staging a build that was rolled back before is refused with a message (#7). Docs: run-file path, hard-link warning.
- Fixed: UI freezes in scans and playback starts - the playing song's ID is cached per track and looked up on a short-wait
  read-only connection (#1).
- LVGL asserts abort with a crash log line instead of spinning; cover decoding budgets to available memory (#2).
- Hold menu, file confirm/picker, EQ number pad and "Add lyrics & artwork" close on every screen change; settings rows,
  mode screen and play-mode index hardened (#6).
- Rename/move carry favourites, play stats and book progress; long names not truncated; long playing paths guarded;
  replacing a folder's ancestor refused (#5).
- Tags: covers reused only within one album folder; unsynced .lrc no longer hides embedded synced lyrics (#4).
- Ogg/Opus and FLAC comments streamed (large covers no longer break tags); oversized ID3 flagged as a read error (#3).
- Quick Settings: Current renamed List; List rows curve and sit like the Library's.
- Music: wider title, artist and progress line. Library: rows 52 px, text one size larger at every Font Size.
- Navigation circle Double tap / Hold moved to Disco Options > Layout; new Volume, Screen off, Play/Pause, Next track.
- One "Restarting the screen - your music keeps playing" notice; Shortcuts swipe tip; "Number Before Title".

## [1.4.0] - 2026-10-12

- Quick Settings (Current style) and Working Modes follow the free edge of the screen, with one font size per list; Left mirrors Right.
  Buttons stay clear of the back-swipe edge, and a swipe can no longer also press the button it started on.
- Quick Settings: Current or Arc style, with editable items (Disco Options > Quick Settings).
- Open navigation circle: configurable Double tap and Hold (System > Controls). First-use hints (System > Help).
- Disco is first in the theme list; Progress Shape moved to Disco Options > Layout.
- Battery: open gauge on the navigation side, centred heading, larger estimate.
- Faster track changes: covers, album colours and EQ reads prepared in the background; lower memory use for large covers.
- Optional apps: Disc Shelf 1.0.4 (new) and Album Roulette 1.3.1, installed separately.
  See [Disc Shelf](docs/DISC_SHELF_INSTALL.md), [Album Roulette](docs/ALBUM_ROULETTE_INSTALL.md), [SD updates](docs/UPDATING.md)
  and the [release notes](docs/releases/v1.4.0.md).

## [1.3.2-polish9] - 2026-10-12 (dev candidate)

- Cache the 364px sharp cover alongside existing artwork; existing entries remain
  valid and gain the optional sharp picture on demand. Online artwork stays separate.
- Prepare image pixels and Disco backdrop tint off the UI thread; publish before
  best-effort SD cache writes. Bound memory and discard superseded results.
- Reuse unchanged Disco art across track changes; no repeat tint/rebind for the
  same art/appearance/idle/accent state. Music layout remains unchanged.
- Give live artwork priority over new prewarm attempts, with a two-second grace
  after transport taps. In-progress bounded decoding is not forcefully interrupted.
- Queue play-history writes on a sleeping worker with a dedicated SQLite connection;
  refresh Song Info's metadata only while the page is visible.
- Temporarily deliver pending artwork on a 30ms timer instead of 120ms, restoring
  120ms once work completes. Optional DISKOS_PERF logs, no continuous tracing by default.
- Native rapid-skip, stale-result, cache/SD ownership, SQLite contention and
  ASan/UBSan UI checks pass; static MIPS candidate built. Device performance,
  battery impact and acceptance remain unmeasured/pending.

## [1.3.2-polish8] - 2026-10-12 (dev candidate)

- Current QS action pills move beyond the edge-Back zone with an 8px buffer;
  mirrored for Left. Compact curved rows and equal gaps remain.
- Modes containers are shorter and inset from the same protected edge, with
  adaptive single-line labels and existing 56px row heights.
- Radio positions, Arc and mode-switching logic unchanged. Native both-side
  checks pass; matching static MIPS dev payload, device acceptance pending.

## [1.3.2-polish7] - 2026-10-12 (dev candidate)

- Compact uniform Current QS pills sized for the longest abbreviation, keeping
  curved placement and bold single-line labels. Exact equal gaps include the
  space below Wi-Fi/BT; the full list uses 4px gaps.
- Arc radius reduced to 122px for more clearance from the brightness/battery arcs.
- Fixed radios and 66px Arc buttons remain; both-side native checks pass.
  Matching static MIPS dev payload supplied; device acceptance pending.

## [1.3.2-polish6] - 2026-10-12 (dev candidate)

- Restore Current QS list curvature around the bezel/navigation field, with
  left-aligned bold captions, icon clearance and centred bottom pill.
- Spread Arc targets onto a larger 128px concentric radius. Fixed Wi-Fi/BT,
  66px Arc targets and equal Current row spacing remain.
- Includes previous dev work and matching payload. Native both-side checks pass;
  physical-device acceptance pending.

## [1.3.2-polish5] - 2026-10-12 (dev candidate)

- Current Quick Settings uses short bold single-line captions and equally sized,
  evenly spaced rows. Radio pills are fixed by action, regardless of item order.
- Adaptive 22/18/16px bold type; smaller icons for densely populated lists.
- Arc layout unchanged. Includes all polish4 work. Native both-side checks pass;
  physical-device acceptance pending. Matching dev payload supplied.

## [1.3.2-polish4] - 2026-10-12 (dev candidate)

- Includes open-menu Hold/Double tap controls and first-use hints from earlier dev work.
- Disco Quick Settings: Current/Arc styles, eight 66px circular targets with
  inset concentric spacing and reserved inner Wi-Fi/Bluetooth controls.
- Shared item editor supports eight selectable/reorderable actions; live radio
  indicators, hold-to-settings and inexpensive press feedback.
- Left mirrors Arc; Current supports all eight selections with compact rows.
- Matching rebuilt dev payload and metadata. Both-side LVGL checks and static
  MIPS ABI checks; physical-device acceptance pending. Main remains 1.3.1.

## [Disco! 1.3.1] - 2026-10-11

### Added
- Disco Song Info as an album back sleeve, accessible through Info in Music's title menu.
- Track Font: Original, Inter Semibold and Nunito Bold, with larger alternate Music type and existing international/CJK fallbacks. Bundled font sources, OFL licenses and generator.
- Session-only Library scroll memory for twelve views, transient position/count hints in long lists and queues, and brief reveal of highlighted long titles.

### Changed
- Larger action targets, icons and captions across track/group/file menus and confirmations; roomier Music actions.
- Bigger Disco Shortcuts and All apps rows, labels and pills; clearer pressed feedback in Quick Settings, Modes and Shortcuts.
- Queue: larger Play/Random/Clear, highlighted current track and separator, with tiny explanation and duplicate headings removed. Empty controls disable; Random requires two entries.
- Disco EQ spreads bands across the screen, enlarges labels and preset, says "Choose a band", and adds a back chevron before the preset.
- About and shipped payload now show 1.3.1. Website, screenshots, tour and update examples refreshed.

### Validation
- Owner approved the polished3 interface after physical-device testing. Final version rebuild checked on the host; no separate battery measurement.
- Production static MIPS ABI, theme/punctuation checks, LVGL layouts and focused interaction checks. See [release notes](docs/releases/v1.3.1.md).

## [Disco! 1.3.0] - 2026-10-10

### Added
- Left-handed mode: Disco Options > Layout > Side (Right / Left, Apply restarts the interface). Mirrors the navigation circle, its sliver and hint, Quick Settings, Weather, Working mode, Shortcuts, the Equalizer start page, Battery, list clearance and rim dots; the back swipe starts at the right edge. Arcs keep their direction; in Left the progress arc starts above the circle and still runs clockwise.
- Scroll styles for Disco lists: Classic, Straight and Disco (a CD rim around the navigation circle, times in one straight column).
- Idle Music: with nothing playing (10 s after start, or at once with Resume Playback off) a generated CD replaces the cover and backdrops, and "Go to Library" replaces the title.
- Hold the top of Music to show or hide the clock, weather and status icons.
- Album hold menu: Tags (tag every track of the album).
- Song Info: bitrate beside the file size (stream rate, else the file's average).

### Changed
- Settings reorganised into short sub-pages: Display (Theme, Text & Layout, Standby, Online & Extras), Disco Options (Music Screen, Colours, Layout) and System (Power, Date & Time, Library, Controls, Maintenance, About). Back steps up one level.
- Disco long-press menus use glass buttons, accent icons and a glass Cancel hub over the cover backdrop.
- "Since full charge" counts from 95% (80% with Charging Limit).
- About shows Disco! 1.3.0; the upstream version row is removed from About and Device Info.

### Fixed
- The settings store held 256 entries; a well-used Disc filled it and new settings failed with "Couldn't save settings". Raised to 512.

## [Disco! 1.2.3] - 2026-10-09

### Changed
- Enlarge Disco Modes rows to 56px and labels to 22px, fitting the rounded bottom row closer to the rim.
- Expand signed SD-card update instructions, including key reuse, verification, trial acceptance and rollback.
- Exclude generated Unicode font descriptions from the punctuation check while retaining application-source and header checks.

### Fixed
- Keep the Immersive cover opaque throughout Disco's return to Music, preventing the shared Ring layout from flashing through. Cancel stale close callbacks when reopening.
- Refuse Disco Immersive navigation when there is no track to display.
- Preserve Music Assistant server and weather location settings when cancelling their editors; distinguish cancellation from saving an empty field.
- Identify app shortcuts by their installation directory instead of display names, supporting duplicate and long names while retaining unambiguous legacy shortcuts.
- Use exclusive temporary files for copying and stage complete replacements before removing the old destination. Restore the original on failed installation and retain its backup if rollback fails.
- Protect currently playing replacement destinations, including folders, and recheck playback in the file-operation worker.

### Validation
- Production MIPS build, MIPS32r2/FP64/NaN2008 ABI, embedded 1.2.3 version, theme and punctuation gates verified.
- 44 focused LVGL scenarios, filesystem/shortcut regressions and existing config/playback-state tests passed. Signed-update verification passed and binary tampering was rejected with disposable test keys.
- Physical-device verification remains pending. No tag or release has been published from this local build.

## [Disco! 1.2.2] - 2026-10-09

- Ship Album Roulette separately under `apps/album-roulette`, removing its executable and installation routine from mq_ui.
- Retain the vinyl menu entry, themed landing screen and all 1.2.0 scrolling/navigation changes.
- Include Album Roulette 1.3.1 with a shared fallback image and bounded uncached artwork heap; decoder safeguards remain.
- Publish a complete release ZIP, separate user-app archive and updated installation instructions. UI-only updates no longer carry the app.

## [Disco! 1.2.1] - 2026-10-09

### Added
- **Album Roulette 1.3.0 is now bundled inside mq_ui**, including UI-only updates. Missing app files are installed
  automatically before Apps is populated. Existing executable/config files are preserved; nothing auto-launches.
- App corresponding source, dependency licenses and rebuild instructions are included in the repository.

### Fixed
- A fresh installation can now use the Roulette landing screen without a separate app download.

### Validation
- App and UI cross-builds and the production theme gate passed. A host extraction check verified exact bytes,
  executable permissions and preservation of an existing app/config. Final device testing is still pending.

## [Disco! 1.2.0] - 2026-10-09

Settings > System > About shows "Disco! 1.2.0".

### Added
- **Rediscover your collection with Album Roulette:** watch round covers spin past, accept a winning album
  with Play! or a cover tap, and listen in sequential order. Long-press the winner to return for another spin.
  The separately installed app supplies the roulette experience; 1.2.0 makes it a consistent part of the UI.
- **Album Roulette in the main menu**, with a vinyl icon. Add it through Settings > Display > Disco Menu.
- **Album Roulette landing screen** with a theme-aware vinyl, a short introduction and an **I feel lucky!**
  button. The main menu, Apps and app shortcuts all open this screen before launching the installed app.
  Album Roulette remains a separate installation at `/usr/data/apps/album-roulette/app`; a missing app
  produces a clear message. This release does not bundle the app itself.

### Changed
- **Smoother scrolling throughout the UI:** a lighter 10 px touch threshold and continuous curved-row
  sizing in Library and shared lists, including Settings. Existing momentum, theme rules and windowed
  large-list handling remain in place; only rows around the viewport need live UI objects in long lists.
- **Navigation drawer:** a 180 ms opening and 140 ms closing motion, shortened when reversing partway.
  Reopening during a close continues from the current position. It remains a position-only animation.
- **Quick Settings:** the Wi-Fi/Bluetooth row moves 4 px upward to improve clearance.
- **Working mode:** 52 px rows, 20 px names, 22 px icons and 18 px status glyphs; all five modes remain visible.

### Validation
- Static MIPS32r2 FP64 NaN2008 build and the production theme check passed.
- Landing-page previews were rendered with LVGL. Physical-device verification of this final release build
  is still pending; no new hardware or firmware compatibility is claimed.

## [Disco! 1.1.1] - 2026-10-07

Settings > About shows "Disco! 1.1.1".

### Added
- **A - Z in Folders**, as in the Library lists (folders with more than a dozen entries). The pill is bigger
  everywhere and easier to hit.
- **First-time hint:** a small "Menu" pill points at the navigation circle's sliver until the circle has been opened
  once (a few seconds after each start-up; a tap on it opens the circle).

### Changed
- **Battery** shows playing and screen-on time **since the last full charge** (100%, or 80% with Charging Limit on;
  charging while switched off counts too), with how long ago that was. Until a full charge has been seen, the last
  24 hours as before.
- **Back to Artists** (from an artist's albums or songs) lands on that artist, focused, instead of the top of the list.
- **Bluetooth Codec** list: "Best effort: SBC if the headphones lack it."
- **MA Sendspin** says "Wi-Fi is off" (a toast when you turn it on, and on its screen) instead of only looking for
  Music Assistant.
- **Ring:** button rings follow the Accent colour you picked (red / lime only with the album accent).
- **Braun:** every knob's pointer stands straight up in the icon colour, like an OFF switch (switches still turn it
  to 45 degrees when on); the equalizer's switch too.

### Fixed
- **Add to queue / Playlist on an artist** missed songs: under Artists by Album Artist it matched the track artist,
  and under Artists it missed songs credited to several artists ("A, B"). Both now take exactly the songs the
  artist's page shows.

## [Disco! 1.1] - 2026-10-07

Settings > About shows "Disco! 1.1".

### Added
- **MA Sendspin (experimental).** The Disc as a Sendspin player for [Music Assistant](https://www.music-assistant.io/):
  it shows up there as a speaker, and Music shows what Music Assistant plays (title, artist, cover, progress,
  Immersive); play/pause, next, previous and seeking on the screen control Music Assistant, and the Disc stays awake
  while it plays. The player can't own the Disc's audio, so the sound goes through the Disc's own AirPlay receiver;
  turning Sendspin on switches to AirPlay, another working mode turns it off. Settings > Network > MA Sendspin: on/off,
  Server (Auto finds Music Assistant on the network, or type its address), Player Name, Sync Delay (-2000 to +2000 ms,
  to line it up with other speakers). Add **Sendspin** to the navigation circle under Disco Options > Disco Menu.
  Experimental: works with Music Assistant 2.10 (its "legacy" Sendspin connection); a later Music Assistant that drops
  it will need a Disco update. The Disc's physical buttons still control the built-in player.
- **Track Number** (Disco Options): "3. Northern Lights" on Music, from the song's tags.

### Changed
- **Quick Settings (Disco):** five rows of taller pills with bigger text. Wi-Fi and Bluetooth share the top row as
  icons with a round On/Off badge (filled with the accent when on).
- **Typing** (Wi-Fi password, Sendspin server and name, playlist names) uses a big keyboard like Search's, with capitals
  (Aa) and two symbol pages, so every character a password may need is there.
- **Volume Keys:** the three press options are in their own submenu, Settings > System > Volume Keys.
- **Online updates removed:** Update diskOS and Automatic Updates are gone. Update from the SD card or by flashing.
- **Bluetooth Codec** shows an arrow and opens a list; the codec's name ran into the label.

### Fixed
- **Quick Settings showed Wi-Fi as on** while the radio was off (it showed the saved setting); the tile and its tap now
  check the real radio.

## [Disco! 1.0.3] - 2026-10-07

Settings > About shows "Disco! 1.0.3".

### Added
- **Ogg Opus (.opus, .oga).** Indexed with their tags and exact lengths, shown in Folders, and played by the stock
  decoder; Now Playing shows the tag title instead of the filename. Ported from diskOS PR #19 by
  [ingvarr777](https://github.com/ingvarr777) - thank you. (.opus verified on V2.57; .oga untested.)

### Fixed
- **AirPlay and Bluetooth streaming** could not be switched on from Working mode since the move to diskOS 1.2.0 (the
  tap was silently refused). The commands for both are back.

## [Disco! 1.0.2] - 2026-10-07

Settings > About shows "Disco! 1.0.2".

### Fixed
- **"Player didn't respond" a few seconds after start-up.** diskOS turns the player's own idle power-off off at start-up
  (it runs its own); that request is sent before the player is ready and repeated until the player confirms, and every
  early attempt raised the toast. It now waits for the player quietly. Real failures of things you do still show it.
- **A fresh flash now always runs the build you flashed.** Builds installed from the SD card are kept on the Disc's data
  partition, which survives a flash, and the start-up script preferred them over the flashed one - so after flashing, an
  older SD-card build could keep running. The script now recognises a new flash (by the flashed build's checksum) and
  drops the kept SD-card builds once; SD-card updates installed afterwards work as before.

## [Disco! 1.0.1] - 2026-10-07

A stability fix for talking to the player, and a CD look for Immersive. Settings > About shows "Disco! 1.0.1".

### Added
- **Disco Options > Immersive: Vinyl or CD.** CD replaces the grooves and dark label with a clear CD hub, a faint
  rainbow sheen over the disc and thin rainbow spokes. The sheen and spokes stay still while the cover spins (like
  light on a real CD); the dark shade behind the lyrics is the same as with Vinyl.

### Changed
- Working mode: "Local playback" is now "Playback"; while a mode changes, spinning arrows replace the "Switching..."
  text (and a tap during a switch is ignored instead of toasting); taller rows in Disco.
- The mode screens (USB storage, USB DAC, Bluetooth DAC, AirPlay) drop their "Local" button; "Modes" leads back.

### Fixed
- **"Player didn't respond" at start-up, and playback that sometimes wouldn't start** when changing tracks or starting a
  list. The player's command mailbox holds 20 messages and the player stops reading it while it is busy (starting up,
  rebuilding a playlist). Background "what's playing?" requests filled it up, and a command that didn't fit was dropped
  after ~15 ms - sometimes half of the three-step play start (output, mode, play), which left the player unable to play.
  Now a command that doesn't fit waits, in order, and goes out as soon as the player reads again (it only fails, with
  the toast, after 6 s of a silent player); background requests are only sent when the mailbox is nearly empty; and
  the 6 s "Couldn't start playback" timer starts when the play command actually reaches the player.
- At a cold start the player creates its mailbox only after a while; commands sent before that now wait for it (up to
  30 s while the player hasn't answered yet) instead of failing at once with "Player didn't respond".

## [Disco! 1.0] - 2026-10-06

The fork becomes **diskOS Disco!**, with the new **Disco** theme as the default. Built on Fork 1.2.4 (diskOS 1.2.4
underneath). Settings > About shows "Disco! 1.0". Ring, Braun and Stone are still in Display > Theme.

### Added
- **Disco theme** (dark and light, plus Automatic Appearance): the playing album's cover as the interface; sharp on
  Music, blurred and tinted behind every other screen; glass panels; an optional CD-rainbow rim sheen.
- **The navigation circle**: a shard on the right rim that opens into a circle for moving between sections; hold for
  Music. Its sections are configurable (Display > Disco Options > Disco Menu); Music and Settings can't be removed.
- **Music**: clock, status icons and weather over the cover; large title and artist (scroll five times, then "...");
  a seekable rainbow progress line with elapsed / remaining times; transport on an arc; a play-mode button that cycles
  modes with a toast; text and fades that stay readable on any cover.
- **Title menu** (tap the title): Favourite, Album (the song highlighted), Artist (the album highlighted), Queue,
  Lyrics, Immersive.
- **Disco Options** (Display): Disco Menu, Show Clock, Track Times, Rim Sheen, Equalizer colours, Volume colours,
  Progress Bar (Disco / Accent / Off), **Progress Shape** (Linear, or an Arc around the rim from just below the open
  menu, clockwise, to just above it), **Title Position** (Centred / Left), **Title Hold** (Immersive / Favourite /
  Nothing).
- Disco versions of Quick Settings, Settings, Equalizer (rainbow faders), Volume (hub field + rim arc), Standby,
  Weather (current conditions + next 12 hours), Working mode, Shortcuts, Search (landing page with recent searches,
  full-screen keyboard, artists first), Queue / Up Next, Battery, the back / home swipe bubbles and the rescan dot.
- **Battery** page: charge ring, time left at the recent pace, playing and screen-on time for the last 24 hours.
- Search finds artists as well as songs.
- A crash recorder (the last screens and the crash address) in the boot log, for diagnosis.

### Changed
- Music is first in the navigation dots; Settings > Display hides Now Playing / Disc Colour / Saver Style in Disco.
- Settings re-sync after a player restart is silent and retried, so the "Player didn't respond" toast is gone at start.
- Helpers (update checks, downloads, Last.fm, the Bluetooth codec probe, app launch, restart, RTC save) start with
  posix_spawn instead of fork(), which can fail for memory with a UI this size; the Default UI choice is written by a
  small thread.
- Battery history keeps about 25 hours (what the screen uses), so each save writes 12 KB instead of 80 KB.
- Settings are only written when something changed.
- Lighter drawing: the navigation-circle glow, the Quick Settings cover glow and the progress knobs are plain rings
  instead of blurred shadows (those were redrawn on every frame or every second); the progress arc draws its track
  only where there is no fill.

### Fixed
- **SD-card updates were always rejected as "damaged"**: the UI starts with stdin/stdout closed, so the update folder
  could land on fd 0/1 and be replaced by the checker's own output. Descriptors 0..2 are now filled at start, and the
  last attempt is written to `/usr/data/updates/last_result`.
- An update checker that couldn't run (not started, timed out, killed) now says "Couldn't check the update" instead of
  "damaged"; update checks have a time limit and can no longer wait on an unrelated helper.
- Progress maths overflowed on tracks longer than about 36 minutes.
- Top-layer panels (title menu, Disco Menu editor) closed on any screen change (standby, keys).
- Search: recent searches use the full text; results are kept only when coming back to Search.
- Shortcut icons for Lyrics, Last.fm, Settings and apps were missing.
- Seeking could stay stuck "scrubbing"; the back / home bubbles showed while waking the screen or in Immersive and
  stayed armed after the swipe had become too slow to count; a bad saved swipe distance could divide by zero.
- Text overlaps in Working mode ("Switching...") and Quick Settings ("Scanning").
- Ring / Braun: Bluetooth streaming was routed as USB-audio output.
- Curved rows didn't give a shrunk value its width back; the title menu showed the cut-off title.
- Restart failures are noted in `/tmp/restart_last` with the real cause (was on the card, with a stale error).
- Battery estimate: stops at gaps, charges and level rises; no 24-hour totals without a set clock.

## [Fork 1.2.4] - 2026-10-05

### Added
- Hold a track in Songs, an album, or an artist's / genre's songs: Add to queue, Playlist, Favourite, Album, Artist, Info, Tags.
  A tap plays it on release only, so a hold never starts the song.
- Search: a Search button between the field and the keyboard; nothing is searched until it is pressed. Results open on
  their own page (title, artist · album, curved rows; tap plays, hold opens the track menu) with a back header that
  returns to the keyboard with the text kept.
- Backspace key on the search keyboard (tap = one character, hold = clear).

### Changed
- Search keys 46 px tall with 24 px letters, rows of 7 · 8 · 7 · 6 inside the circle.
- Action menu titles stay on one line ("...") instead of wrapping into the first item.

### Fixed
- The library-rescan runner around the rim is back (lost in the 1.2.0 merge).
- Ring ignored the Accent colour: a picked colour now wins over the album colour on Now Playing, Home, Quick Settings
  and the other music surfaces. Album mode is unchanged; Ring button borders keep the interface colour; Braun stays orange.

## [Fork 1.2.3] - 2026-10-05

Built on Fork 1.2.2. `mq_ui` md5 `de4915d4…` (the device-tested `89da2467…` build with the version set to 1.2.3).

### Added
- Hold an album, artist or genre: Play, Shuffle, Add to queue, Playlist (Add to queue implemented for whole groups).
- Swipe up from the bottom edge goes Home.

### Changed
- Settings > About shows 1.2.3 (was still 1.2.0).
- Library lists curve with the screen like History; the A-Z button moved to the edge; hint "Hold an item for more".
- Ring Now Playing: title 28 px, artist 20 px, matching Braun.
- Ring buttons: 2 px ring in the interface accent (red / lime on Ring Light), glyphs in the album colour.
- Braun ON/OFF knobs: pointer up in the icon colour (off) or 45 degrees orange (on); the lamp is gone.
- Braun Dark volume popup uses the charcoal surfaces.

### Performance
- Immersive: no drawing while the screen is dimmed or off; frames paced on render + compose + flush.
- Immersive: lyrics, hint and progress ring baked into the frame, refreshed only when they change.
- Immersive: rotation loop without per-pixel bounds tests (identical output).
- Theme pass skipped on frames where nothing was created (one-line counter in vendored LVGL).

### Fixed
- The EQ dial keeps its touch instead of starting a page swipe.

## [Fork 1.2.2] - 2026-10-05 - "SD card update"

First fork release on diskOS 1.2.0. Device-tested build: `mq_ui` md5 `84be467f…`.

### Added
- Update from SD Card: signed builds from `<SD>/diskos-update/`, verified on the Disc and installed at
  restart as a trial with automatic rollback. Owner key scripts in `ui/tools/owner-keys/`.
- Ring and Braun themes (with Ring Light, Braun Dark and Automatic Appearance) on top of upstream 1.2.0.
- Themed shutdown screen on a power-key hold; Ring screensaver styles.

### Changed
- Covers decoded inside `mq_ui` instead of the `diskos-artdec` helper.
- Theme list shows Ring, Braun and Stone; the other upstream themes are hidden, not removed.
- Upstream's Wi-Fi release key removed from `payload/`: the Disc trusts only the owner's key.

### Fixed
- Covers stopping after quick skips or album switches.
- Immersive record stuck at about 1 fps.
- Ring missing from Now Playing and Saver Style.
- Brightness reset after screen off/on.
- Equalizer values (the 1.2.1 EQ fix).

---

*Upstream diskOS history below, kept as released by b0hemia.*

## [1.2.0] - 2026-10-02

Adds V2.57 firmware support, more library and display choices, and signed in-app diskOS updates.
Installing 1.2.0 requires the complete installer and a new flash.

### Added

- Install from unmodified official V2.57 firmware, alongside V2.09, V2.28, and V2.40. The
  installer checks the Disc's existing kernel and recovery against the image before writing.
- Seven themes alongside Default, dark and light appearance, Outdoor Mode, font sizes, and an
  interface language choice with English fallback for missing text.
- Up Next shows the current queue and lets you jump to an upcoming song. Artists and Genres can
  open albums or All Songs.
- Library indexing for AAC, OGG, APE, AIFF, WMA, DSF, DFF, DTS, external CUE sheets, and SACD ISO
  track rows. Indexing does not guarantee playback of every format.
- Song Info with available audio and file details; settings for Album Artist grouping, Play
  Through Folders, track numbers, online album art, and online lyrics. Local and embedded lyrics
  remain available when online lyrics are off; timed lyrics can follow playback.
- Song lengths in library song lists and Up Next, read from the file when the library is scanned.
  Songs whose length can't be read show no length.
- Manual date and time, time-zone choice, and Automatic Time. A new startup animation has a
  selectable Disc body colour.
- On V2.57, screen rotation, volume-key actions, idle power-off, a charging limit, and a choice
  to pause or request shutdown when the Sleep Timer ends.
- Bluetooth Codec selection on V2.57. On other firmware the row says "Not on this firmware".
- Device Info, battery/board temperature, and a reset for diskOS appearance and behavior
  settings that keeps music, network, Last.fm, EQ, and audio settings.
- Signed in-app diskOS updates for later releases. The 1.2.0 release image includes the diskOS
  release key by default; its GUI checkbox, "Allow diskOS updates over Wi-Fi", starts checked.
  Untick it or use `--no-ota` to omit the key; `--ota-key PATH` uses your own public key.
  Settings > System > Update diskOS updates only the diskOS app, not stock firmware or the whole
  image. Allow diskOS Updates can stop downloads; turning it off with a staged update offers
  Discard or Cancel. A trial update offers Keep or Go back and rolls back if it fails to start.
  The first update over Wi-Fi will be a release after 1.2.0.
- A macOS release package for Apple Silicon with prebuilt flash tools.

### Changed

- Album artwork is now decoded by a new helper, `diskos-artdec`, instead of the stock `ffmpeg`
  command (which stock V2.57 ships without image decoders). It runs as a separate short-lived
  process and covers embedded and same-folder artwork on every supported firmware. Cached covers
  are rebuilt once after the update.
- Restart is back in Settings. It waits for diskOS card work and a successful flush; if either
  cannot finish, it reports the problem and leaves the Disc running.
- Library rescans show progress and a Stop action. Cancelling keeps the previous library.
- Playlists retain their order and appear in the stock playlist registry. You can add a displayed
  album, artist, or genre song list, remove a song, and rename, export, or delete a playlist. M3U
  import searches card folders within limits and preserves a differing same-name playlist.
- V2.57 Favourites use the player's favourite queue and row identity. Gain is available on V2.57;
  its Custom EQ editor is view-only.
- Update from SD Card identifies a file named like a stock update and explains the stock route;
  diskOS does not run that update.
- Wi-Fi lists place the connected network first and handle encoded names, passwords, and
  connection feedback more carefully.
- Setup and tool builds now work on Arch, CachyOS, and Fedora as well as Debian and Ubuntu;
  `doctor` identifies the Linux distribution.

### Fixed

- The installer reports a USB-permission problem when it sees a mask-ROM Disc but cannot open it.
- Saving the clock no longer waits on the interface loop for the RTC.
- Album cover caching no longer depends on an inode that can change for the same card file.
  Saved accent colours can refresh when cover content changes.
- Playlist export uses a unique temporary file instead of a fixed name that could overwrite a
  user's file.
- A refused playback send no longer opens Now Playing as if playback started. Delayed local-player
  setup no longer interrupts a song that has already begun. CUE and SACD track replay avoids a
  timeout route reset that could stop a live stream.
- A back swipe starting on a control no longer also taps that control, which could go back two
  screens or change a setting.
- Long one-button messages are no longer covered by the OK button.
- With Weather on Home off, diskOS no longer makes a weather request at startup.

## [1.1.3] - 2026-09-24

A safety release. The stock FiiO player, which diskOS runs alongside, tries to unmount the microSD
card and then deletes everything at the card's mount point, without checking that the unmount
worked. If the card is still mounted, this can delete its contents. The flaw is confirmed in the
stock player on V2.09, V2.28, and V2.40. It has not been proven to be the cause of the two reported
card wipes. Every diskOS user should update; updating requires the complete installer and a new flash.

### Added

- A delete guard in the flashed image, at `/bin/rm` and first on the stock player's `PATH` (so the
  stock fallback and watchdog launches are covered too). It refuses `rm` on the card or anything on
  it; the card's folder can only be removed when it is empty and unmounted. It blocks this specific
  deletion path, not every possible way to lose data.
- A boot selector that runs before diskOS starts and decides once per boot between diskOS and stock.
  Any missing or invalid result boots stock.
- Hold Volume Up for about three seconds to force-close an external app. A message says if this is
  unavailable.
- Extra punctuation in the 14, 16, 18, and 20 px interface fonts, and eight more CJK characters in
  the fallback font. This is not full CJK support.

### Changed

- USB Storage now waits for diskOS to finish reading and writing the card, confirms the card really
  was handed over and really came back, and only then resumes artwork caching. A pending handoff
  survives a UI restart.
- USB Storage is refused during a library scan, and whenever diskOS cannot confirm that the card is
  protected or who owns it. If a handoff is left uncertain, diskOS keeps card access off and asks for
  a reboot.
- Library scans, browsing, artwork, lyrics, book details, playlists, and playback commands wait for
  these card checks. If protection is unavailable, diskOS keeps its card features off and says so;
  the stock player still starts.
- On V2.40, cold-boot card handling tries the direct mount before sending an insert event.
- First-boot setup uses only the UI built into the flashed image; the SD-card fallback copy is gone.
- Restart is temporarily removed from Settings until it can wait for card writes to finish.
- Bluetooth starts off when there is no saved diskOS preference. Saved preferences are kept.
  Turning it on from Quick Settings now waits for it to become ready.
- Home, Now Playing, and Quick Settings share the same transport controls. The play/pause icon
  responds at once. Quick Settings skips 15 s back and 30 s forward in audiobooks.
- Quick Settings allows the transport row only with five or fewer tiles enabled.
- Settings values sit below their labels, controls use the current accent colour, and search fits
  the round screen better.
- Debug Mode's description now says SSH over Wi-Fi; the USB serial shell is for developer builds.
- The installer accepts only the unmodified official V2.09, V2.28, and V2.40 firmware for install
  and build. The untested-firmware override no longer applies to them. V2.57 is refused.
- Release packaging requires the boot selector, its probe and helper, and the delete guard, and the
  image check verifies all of them and the patched boot script.

### Fixed

- Stopping card access now cancels a running scan and all artwork work. A cancelled scan keeps the
  previous library.
- Artwork cache files are flushed to the card before they are used (where the card supports it);
  artwork and playlist exports also try to flush their folder.
- Player start-up and the hand-off to stock have firmer time limits, clean up their helper
  processes, and reset signals correctly.
- Default UI's boot flag is written by a time-limited helper that reports failure.
- Bluetooth status and audio-route checks are time-limited: the check is killed along with anything
  it started, and cleanup never waits without a limit. Bluetooth shutdown no longer waits on the
  Bluetooth service. This closes one way the screen could freeze, not every possible one.
- Scan messages now tell apart refused, running, failed, and finished scans, with a count of skipped
  files. Help hints no longer replace an active message.
- Failed playback, seek, and playlist-export requests now report failure.
- The mode picker keeps showing "switching" until a mode change finishes, and clears the checkmark
  if it fails.
- The boot log keeps earlier start-up messages until it reaches its size limit.

## [1.1.2] - 2026-09-20

### Fixed

- Install now works from a plain source checkout that does not include the bundled squashfs tools:
  the installer uses a system `mksquashfs`/`unsquashfs` from `PATH` (for example from the
  `squashfs-tools` package) when the vendored copies are absent. `usbboot` and the device files
  remain bundle-only. Previously a source checkout failed with "bundled tool 'unsquashfs' not found"
  even when squashfs-tools was installed.
- Added a squashfs LZO capability check that runs before the build and in `doctor`, so a squashfs
  tool built without LZO support fails with a clear, actionable message instead of failing partway
  through the build.

### Changed

- `doctor` now labels each native tool as bundled or system, flags broken entries, and reports the
  squashfs LZO check result. Tool-not-found guidance points at the correct per-platform build step
  instead of advising a re-download.

## [1.1.1] - 2026-09-20

### Fixed

- Fixed rootfs extraction on the default case-insensitive macOS filesystem, resolving E230
  `unsquashfs ... already exists` failures from filenames that differ only in case.
  The installer automatically creates, mounts, and verifies a case-sensitive APFS scratch image
  using `hdiutil`, uses it for extraction and validation, then detaches and removes it.
  Cleanup failures are reported.
- Kept the existing build path unchanged on Linux with a case-sensitive filesystem and on
  case-sensitive macOS volumes. Validated on real macOS with `back_home.png` and `BACK_HOME.png`
  coexisting after extraction, and on Linux. macOS device flashing remains unverified end to end.

## [1.1.0] - 2026-09-20

### Added

- V2.40 firmware support, flash-tested alongside V2.09 and V2.28 on Linux x86-64.
- Album cover flow with horizontal swipe navigation, pre-baked cover sprites, and reflections.
  Dynamic album lists and a bounded sprite cache accommodate libraries with thousands of albums.
- Dedicated Books view for single-file `.m4b` audiobooks with saved listening positions, plus
  chapter navigation from Now Playing (jump to a chapter, current chapter highlighted, with titles
  and durations).
- File/folder browsing on the microSD card, with playback of selected library tracks.
  Playback uses the all-songs queue, not a folder-only queue.
- Bundled Noto Sans glyphs for Latin-extended, Greek, and Cyrillic library text.
- On-device UI source under GPL-3.0-or-later, with vendored LVGL and build instructions.
  Installer, build tooling, and documentation remain MIT licensed.

### Changed

- Increased image and writer capacity from 580 to 768 NAND blocks (96 MiB) to fit V2.40's
  roughly 88 MB stock root filesystem, without changing the partition layout.
- Shortened the conservative flash wait; flashing and verification now take about 15 minutes.
- Separated `.m4b` entries from music views and queues, including migration of existing entries.
- Refreshed the circular listening interface and browsing views.

### Fixed

- Refuse undersized or unrecognized writers before any NAND write, and check reported capacity
  after flashing to detect writer/image mismatches.
- Repad older short saved stock images into a separate copy, then validate them before restore.
- Preserve USER EQ presets changed on the stock player when reselected in diskOS.

## [1.0.0-beta]

### Added

- Initial public release for the FiiO Snowsky Disc, flash-tested with V2.09 and V2.28 on Linux x86-64.
- Circular player interface with local playback, bezel and alphabet navigation, M3U playlist import,
  and album-art dynamic colors.
- Experimental weather and Last.fm integrations.
- Graphical and command-line installer using a local Python environment.
- Local image builds from user-supplied official FiiO firmware archives.
- Mask-ROM USB flashing with a bad-block-aware writer and block verification.
- Saved-stock restore, persistent stock UI selection, one-time stock boot, and stock UI fallback.
- Opt-in SSH Debug Mode and separate public and development installation variants.
- macOS build-from-source path, unverified for release artifacts and device flashing.
- MIT-licensed installer tooling, recovery documentation, hardware notes, and third-party notices.
  The initial release distributed the UI as a binary; UI source publication followed in v1.1.0.

[1.1.3]: https://github.com/b0hemia/diskos/compare/v1.1.2...v1.1.3
[1.1.2]: https://github.com/b0hemia/diskos/compare/v1.1.1...v1.1.2
[1.1.1]: https://github.com/b0hemia/diskos/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/b0hemia/diskos/compare/v1.0.0-beta...v1.1.0
[1.0.0-beta]: https://github.com/b0hemia/diskos/releases/tag/v1.0.0-beta

### Zune development: typography Quick Settings
- Replace round QS buttons with six configurable text actions in two columns; use short labels, inset the left column, and enlarge clock/battery text.
- Restore lower-rim brightness control as a 90-degree arc.
- Add scrolling title/artist heading with pause/resume interaction; stop its animation while hidden or the display is off.
- Preserve existing radio/EQ actions, long presses and copy navigation restrictions.

### Zune development: matching utility screens
- Match five Working Modes controls and four Weather forecast cells to the QS text-grid layout.
- Enlarge Search letters, radial touch targets, Search/Cancel text and results.
- Enlarge and tighten EQ Edit/Rename/on-off controls.
- Match shared clock text to QS and enlarge battery icons. No additional polling, network fetches or periodic timers.
