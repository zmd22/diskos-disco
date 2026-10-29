> Historical material. Not current 3.2.1 instructions; see the current documentation index.

# Native Disc Shelf integration: validation

Development branch based on public Disco 2.6.2 (`e8b3b3a`). No Bluetooth, power-button or timeout-lock changes.
Prepared for Disco 2.7.0. Navidrome has been exercised on the owner’s Disc; SMB3 physical NAS/device acceptance and power measurements remain pending.

## Implemented

- Builtin All apps / navigation / shortcuts entry with the legacy `disc-shelf` identity, hiding old standalone duplicates.
- Native screen, theme-role colours/fonts, compact Library header and spacing, continuous 32-row viewport and position indicator.
- Shared Library swipe recognizer; remote-ID selections survive recycled windows. Only Download is offered. Back cancels selection before navigating.
- Existing local-presence colour; selected missing items show a check, selected local items an SD-card/check marker; partial albums remain distinguishable.
- Reused A-Z grid and letter circle; instance-safe ownership and destruction. Seeking jumps within the whole collection rather than hiding other letters.
- Separate first-use selection hint respecting the theme/global hint setting.
- Server setup with the native keyboard and masked password field; existing private shelf.conf is preserved until explicitly saved.
- Missing-only and explicit Download-again modes. Atomic replacement is restricted to an exact server/track-ID manifest, within the download root. Tag-only local matches are retained.
- Exact album membership cached after download/check; paths checked on worker-side browse. Incremental local-index updates avoid automatic full rescans. No two-way synchronization or periodic catalogue refresh.
- Backend starts only on open; on-demand context, 256 KiB worker stack and condition-variable idle wait. Cancel, close, shutdown and USB handover stop work and release the SD lease.
- No standalone LVGL initialization, display takeover, global signal changes or SQLITE_TMPDIR changes in the native module. The private fetch helper retains parent-death handling.

## Host checks

Run from the repository root:

```
python3 ui/tests/discshelf_backend_test.py
python3 ui/tests/discshelf_lifecycle_test.py
python3 ui/tests/discshelf_registration_test.py
python3 ui/tests/discshelf/run.py
make -C ui CROSS= -j2 host-render
```

The standalone-derived HTTP/SQLite suite covers JSON/Unicode limits, 10,003 artists and bounded viewport windows,
complete list cardinality and A-Z jump offsets, nested navigation, 50,000 local tracks and indexed lookup,
failed-response rollback/retry, streamed downloads, duplicate avoidance, repeated launch, short/error bodies and cancellation.
Its idle worker CPU check measured 0 ns over a 200 ms condition-variable wait on this host.

Additional production-function tests cover default skipping, missing tracks in partial albums, exact-ID replacement,
failed/cancelled replacement preserving the original, and refusing replacement of tag-only matches. The real lifecycle
harness completed 64 worker start/stop cycles with no remaining context or SD lease. `sizeof(shared_t)` is 612,448
bytes on the host; the context and separate 32-row UI array are allocated only while open. Actual peak Disc RSS is not measured.

Native LVGL tests use real touch input to select by swipe, verify a Download-only action, Back cancellation and repeated
preview teardown in Disco, Null and Squash. The existing letter-wheel tests pass in each theme (grid, bidirectional
scrub, wrap, held-touch suppression, 1-second release timeout, Back and option restoration). Real LVGL screenshots
cover browsing, selection, both pickers, confirmation, landing and server setup in the three themes and two appearances.

Existing Bluetooth hold/resume/bulk-favourites tests and queue tests are also exercised. LeakSanitizer is unavailable
in this host runtime; tests use `ASAN_OPTIONS=detect_leaks=0`. Address/undefined-behaviour instrumentation remains enabled
where specified by each harness. Lifecycle release is checked explicitly rather than claiming a working leak-sanitizer run.

## Scope and device acceptance

The release version is 2.7.0, built from public 2.6.2 plus the changes recorded here. Earlier size/host results below describe intermediate development payloads, not a guarantee of physical-device compatibility. Build records identify final source and binary hashes. Nothing is pushed by the local packaging process.

On a Disc, test your real Navidrome catalogue, Wi-Fi failure/recovery, a partial album, an explicit redownload,
card removal/full-card handling, cancellation, repeated open/close, normal playback and switching to USB storage.
Confirm zero network children after exit and compare RSS/CPU before/after repeated opens. Host tests cannot establish
hardware battery consumption, peak runtime memory or physical SD-removal behaviour.

The old standalone archive/source remains available for older firmware; the complete ZIP no longer duplicates its
executable inside `apps/`. Existing device executables/configuration are not deleted, preserving rollback compatibility.

## Final device build size

GCC 11.2 repository toolchain build, static little-endian MIPS32r2/o32, FP64, NaN2008:

- Public 2.6.2 payload: 6,160,032 bytes.
- Native Disc Shelf test payload: 6,219,588 bytes.
- Increase: 59,556 bytes (58.16 KiB, about 0.97%).
- ELF BSS grows by 10,136 bytes; the large app buffers remain on-demand allocations.
- Exact binary/source identity is recorded in payload/mq_ui.build.json.

This does not measure peak device RSS or physical battery consumption.

## SMB3 development follow-up

Final SMB3 test payload: 6,373,628 bytes, versus 6,219,588 for the previous native Disc Shelf payload: +154,040 bytes (150.4 KiB). MIPS ELF text/data/BSS for the preceding equivalent link were 6,297,212 / 72,964 / 4,456,344 bytes, versus 6,146,656 / 71,668 / 4,430,512 for the native baseline. Fixed BSS increased about 25.2 KiB; actual peak RSS/current draw require hardware measurements. Source changes also include the reviewed layout/icon/typography fixes.

Protocol work is confined to the existing on-demand worker. Directory query buffers are bounded to 64 KiB, file reads to 32 KiB, and UI data stays at 32 rows. Idle SMB workers wait on the existing condition variable. There is no automatic discovery, keepalive, sync or refresh timer. Session/password and source-selection details are documented in `docs/plans/disc-shelf-smb3.md`.

## Download display and buffered Navidrome writes

- Download status uses at most three lines with ellipsis, smaller percentage plus
  a four-sample rolling speed, and a larger transferred/total MiB line. Native
  LVGL fixtures cover Disco, Null and Squash; displayed fixture speed is synthetic.
- Speed reads existing byte counters only while the screen is visible. A per-file
  serial resets its samples even for consecutive equal-size files. No new timer,
  network request, directory scan or idle worker is added.
- Navidrome now coalesces small wget pipe reads in a 128 KiB temporary heap buffer
  instead of writing each read immediately (previous stack buffer: 32 KiB).
  Partial buffers flush after a 250 ms threshold (observed within 306 ms in the
  stalled-sender test, plus possible scheduler/storage delay) and at EOF.
  The allocation is freed on all transfer exits. No increased worker stack.
- Resource checks remain at the existing 1 MiB cadence, cancellation is checked
  before each loop and partial write, expected-size/media checks remain, and final
  fsync/atomic publication are unchanged. Progress publication is limited to about
  five updates per second plus completion, rather than every small read.
- `python3 ui/tests/discshelf/transfer_buffer_test.py` verifies byte-exact output
  including a short final buffer, short writes, EINTR, ENOSPC, cancellation,
  SD/resource failure and partial-buffer flushing while the sender stalls.
  Optional `SHELF_BASELINE_NET=/path/to/old/net.c` compares write-call counts.
- A 10 MiB + 18 byte localhost transfer made 81 SD-file write calls versus 326
  before (75.2% fewer in that run). Both runs took approximately 0.035 seconds:
  this proves batching, **not an on-device throughput improvement**. Actual speed,
  power and battery effects require measurement on the Disc with the same server,
  track and SD card. This patch adds no rate limit or parallel downloads, does not
  change SMB read sizes, and does not remove durability or space-reserve checks.

## Shared SongSphere guard

SongSphere now uses the same static guard layout as Disc Shelf, with its own
sphere vector icon, mood-map contours and explicit Open SongSphere button.
Disco/Null menu browsing, All apps and app shortcuts show SCR_APPLAND first;
only the button calls the existing sphere_ui_open(). Direct album/artist/song
tagging remains immediate. The guard creates no sphere engine, DB connection,
worker or animation timer. Native LVGL fixtures exercise all three themes,
preview switching/cleanup, menu and app dispatch, and a wrapped ssdb_open that
asserts zero opens before clicking and one attempt after clicking Open.

## Menu Shuffle! action

The common menu action list now includes Shuffle!, for Disco, Null and Squash.
It is appended after the existing saved actions (Nothing remains 10). Both the
menu shortcut and Library > Songs use the same all-song playback helper: reject
an empty music library, set shuffle mode, choose a random valid list position,
request the stock all-song list and navigate to Music on acceptance. Success
uses the existing nonblocking themed popup with a shuffle icon, Happy listening!
and its existing 2.4-second dismiss timer. Empty/refused requests show no false
success. Direct album/artist/favourites shuffle paths are unchanged.

Native LVGL pointer fixtures hold each theme's real menu target and verify a
single all-song request, shuffle setting, Music destination, popup text and
empty/refused playback handling. Playback acceptance/counts are mocked for these
host UI tests; physical-player validation remains with the test binary. No
background work or library-wide playlist construction is added.

## Included in final 2.7.0: scoped Wi-Fi power-save override

Copy jobs now query `/usr/bin/wl -i wlan0 PM` once on the Shelf worker. A known
mode 1/2 is temporarily changed to 0 and restored after the Navidrome/SMB job
returns, before completion is published. This covers normal completion,
cancellation and error returns, including early exits from SMB recursion. A mode
already at 0, an unknown mode, or an unavailable utility is left alone. Browsing,
idle and app guard screens do not run these commands. There is no reconnect,
background poller, persistent config change or change to the user's Wi-Fi intent.

Fixed arguments use direct process spawning, bounded output and a one-second
helper deadline. Failure to override does not fail a download. A restore is still
attempted after a timed-out/failed setter because the driver ioctl may already
have executed. Restore failure is logged, not retried in an idle loop. Forced
process termination or a driver/radio that is unavailable cannot guarantee
restoration; no permanent power-saving policy is written.

`python3 ui/tests/discshelf/wifi_pm_test.py` uses a fake utility to verify PM 0
(no setter), PM 1/2 restore, unsupported/query errors, missing utility, and slow
query/setter cleanup. Navidrome and encrypted SMB wire regressions also pass.
Real-driver setting/restore acceptance is pending a device test. In the reported
slow-transfer incident PM was already 0; this guard is not claimed to fix that
incident, which cleared after reconnecting Wi-Fi. This guard is included in the final 2.7.0 release bundle, replacing the earlier unpublished local tag. GitHub had no v2.7.0 tag when the replacement was prepared.

## Clean Navidrome filenames

New copies use Artist/YYYY - Album/NN. Track name.ext. The parser retains year
in the existing response/cache rows, and getAlbum metadata supplies a consistent
album year before rows are published or copied (independent of JSON field order).
Missing years use 0000; no extra network lookup or idle work is added. The same
directory helper is used for cover art. Existing ownership paths are retained,
including the previous hashed filenames, so no card migration or automatic
renaming is performed. SMB naming is unchanged.

Only actual collisions add a readable suffix, e.g. (Disc 2) or (2). An unowned
same-size file or existing .part file is not adopted or overwritten merely
because its clean filename matches. The local duplicate lookup distinguishes
known copies of different server/track IDs, preserving repeated titles on
different discs. Filename tests cover Unicode, missing years, 3-digit track
numbers, collisions/temporary occupants, legacy owned redownloads and safe
replacement; the HTTP fixture verifies inherited album year and cover placement.
Navidrome and encrypted SMB3 host regressions pass.
