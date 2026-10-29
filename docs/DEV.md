# Current release: 3.2.1

The prepared payload is rebuilt for this release. See [3.2.1 release record](RELEASE_3.2.1.md) for current testing limits. The records below describe earlier development checkpoints and their historical artifacts.

# dev — experimental / in testing

Development continues from Disco! 3.0.0, commit `721e07ee2266466888d84b92e756fd3a4fc778b1` (`v3.0.0`). Work belongs on **dev**. Do not update main or release tags without the maintainer’s explicit instruction.

This branch is experimental and in testing. The published 3.0.0 release is a separate baseline.

## Zune animation efficiency review

The review preserves the existing animation timing, easing, momentum, font steps, opacity, artwork detail and rendering quality. No animations are removed or simplified. Immersive already uses elapsed-time rotation, cached lyric snapshots, adaptive frame pacing and hidden/off-screen suspension; those paths remain unchanged.

Implemented:

- Cache the original integer bezel geometry once, sharing it between Home, action/settings dials and curved library rows. Each scrolling row now reads a lookup instead of repeatedly evaluating square root/rounding. Both original rounding conventions are retained. The lookup uses 360 bytes plus its initialization flag, with no heap allocation or new timer.
- Cache each action-dial row’s fitted font for its three distance bands. Returning to a band reuses the result instead of measuring the same string again. The cache adds 288 bytes and resets when a label, subline or dial changes.
- Skip redundant font assignments when adjacent bands resolve to the same fitted face. Replacing text refits immediately, including all loop copies.

A proposed Home caption-alignment shortcut was rejected because the frame comparison detected differences. Its original alignment behavior remains intact.

## Validation

- `python3 ui/tests/zune_scroll_test.py`: 122 Home/action-dial frames match the 3.0.0 baseline byte for byte; both bezel rounding rules checked across off-screen and visible positions; settled callbacks produce zero style-change events; replacement text and newly added sublines invalidate fitted-font choices correctly.
- Focused host checks cover Zune Home/focus, main-icon gestures, modal volume, all Immersive entry/exit styles, live Immersive rendering and standby work suppression.
- Theme source gate passes. Static Home/Music/Quick Settings/Settings frames are compared with the baseline in Disco, Null, Squash and Zune.

Build the host renderer before the scroll check:

```sh
make -C ui CROSS= host-render -j1
python3 ui/tests/zune_scroll_test.py
```

The golden scroll hashes in `ui/tests/fixtures/zune-scroll-3.0.0.txt` were captured with the same diagnostic against unmodified 3.0.0 production sources. They cover 61 intermediate offsets in each of two scrolling scenes.

These are host checks, not measured device frame-rate or battery improvements. On the Disc, check long scrolling sessions, cold and warm settings menus, library font sizes, touch response while music plays and Immersive entry/exit.

The tracked `payload/mq_ui` and its metadata remain the original 3.0.0 release artifact. They do **not** contain these source changes. A new target build and device validation are required before distributing a development firmware; the release preflight correctly rejects changed UI sources against the older artifact.

## Shared themes efficiency and stability pass

Reviewed the shared rendering paths used by Stone, Ring, Braun, Disco, Null and Squash, alongside the existing Zune changes. Kept animation durations, easing, momentum, controls, list dimensions, colours and text unchanged.

- Shared lists and library rows reuse the exact original floored circle spans for their three radii. This removes repeated square roots during scrolling, using a fixed 540-byte lookup plus initialization flags; no heap allocation or new timer.
- Null’s menu keeps its existing motion and sampling cadence, but writes text/thumb colours only when they change. The pointer drag/settle fixture reduces colour-property writes from 287 to 4 in each of its three palettes; all captured pixels remain identical.
- Null menu teardown now retires its timer when a parent deletes the widgets first. Later owner cleanup remains safe. Constructor failure to allocate a timer also cleans up the menu rather than leaving a partial handle.
- Updated stale EQ/config stubs in the stability diagnostic so it exercises the current production function names; no EQ production behavior changed.

Validation:

- `python3 ui/tests/theme_efficiency_test.py`: 1,062 animated frames and 56 static Home/Music/Quick Settings/Settings renders match the pre-change dev baseline across 14 theme/appearance/palette combinations. The Null drag test verifies actual movement and counts colour writes separately from layout/style events.
- 100 Null menu rebuilds alternate parent-first and owner-first cleanup; no orphan timers remain.
- `python3 ui/tests/curve_geometry_test.py`: exact cold/warm circle spans, boundary positions and the uncached-radius fallback.
- Focused Null gestures/volume, Squash interaction/performance and Disco navigation checks; the earlier Zune scroll regression remains available.
- `python3 ui/tests/stability_test.py`: EQ validation, asynchronous database saves, failed-apply handling, busy database behavior, single-instance locking, command timeout and descriptor cleanup.
- Theme source gate and diff whitespace checks.

Build first with `make -C ui CROSS= host-render -j1`. The frame baseline was captured against unchanged production sources from dev `0eb90ca`, using the same host diagnostic. These checks establish host regressions and unchanged sampled rendering; they do not establish physical frame-rate, battery life or complete hardware stability. The tracked 3.0.0 release binary remains unchanged.


## Idle shutdown fixes (2.7.0 / 3.0.0 audit)

The released versions share the affected power logic. Development source now rearms
idle power-off after a shutdown accepted by the state machine subsequently fails
before handoff. It waits a fresh full idle interval rather than retrying in a loop.
Pending idle shutdown cancels on input, playback/busy state, disabling the setting,
or player reconnect. A final audio check runs after sync and before the stock
power-off command. Manual and explicit sleep-timer shutdown have no idle guard.
After handoff, the existing protected-card policy still applies; activity cannot
reopen the card while a stock power-off might remain pending.

Player-generation changes invalidate takeover before idle decisions. Prolonged
unconfirmed takeover with idle power-off enabled produces one warning/log per
player generation, then a recovery notification if confirmation arrives. The
stock unsafe timer is never re-enabled as a fallback. Settings explain that
external power, non-Playback modes and active Disc Shelf work suppress idle shutdown.
Shutdown outcomes are logged so a failed attempt is diagnosable with the screen off.

Validation: `python3 ui/tests/idle_shutdown_test.py` compiles production power
functions with simulated time, IPC, audio and card/process boundaries. It covers
retry after drain, sync-start, sync-exit and sync-timeout failures; input and busy
cancellation during drain/sync; late audio; setting disable; player reconnect;
blocked takeover/recovery; invalid settings; manual/explicit sleep shutdown during
playback; and post-handoff card protection. Also run `shutdown_test.py`,
`audio_running_test.py`, `power_plugged_test.py`, and build the host renderer.
These are host regressions, not device power-off validation. The tracked release
binary remains unchanged; a target build and physical checks are still required.


## Unified settings hierarchy

The approved shared hierarchy and anchors/alphabetical/On-Off-last policy are in
[SETTINGS_TEMPLATE.md](SETTINGS_TEMPLATE.md). Theme ownership is explained in
[DISPLAY_SETTINGS_SPLIT.md](DISPLAY_SETTINGS_SPLIT.md).

Seven categories now agree across all themes. Battery & power includes Details,
idle shutdown, charging protection, standby timing, sleep timer and power actions.
The Equalizer opens the existing full profile editor; DSD output remains read-only.
Saved option IDs and handlers are preserved; startup disc colour is removed.
This is a source update; the release binary is not replaced by host validation.
