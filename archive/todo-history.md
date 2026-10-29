> Historical proposals, not shipped 3.2.1 features. See [current targets](../todo.md).

# Disco usability TODO (history, up to 1.5.0 - kept for reference; the live list is todo.md)

Updated: 2026-10-12. Companion: `handoff.md`.

Shelved proposals and their restart notes: `todo-ideas.md` (Windows installer).

The owner authorized this batch before uploading dev. First-use hints are
implemented in candidate 1.3.2-polish2; device acceptance remains pending.

## 1.5.0 (2026-10-13)

- [x] Boomshine built in; installed apps in the Disco Menu; app landing pages; Disc Shelf docs.
- [ ] Owner: SD update, play Boomshine (both modes, Exit, Vol-Up escape), add Disc Shelf to the menu, check scores carry over.
- [ ] Owner pushes main + dev + tag v1.5.0.

## 1.4.1 (2026-10-12)

- [x] Issues #1-#8 fixed (see handoff.md), contributor thanked in README, release notes, changelog, website.
- [x] QS List, wider Music, Library rows/fonts, nav gestures + 4 actions, restart notice, Shortcuts tip.
- [ ] Owner reflashes (S97 changed), tests updates/trial keep, scans, Opus tags (rescan), rename/move.
- [ ] Owner pushes main + dev + tag v1.4.1; close issues #1-#8 with a thank-you comment.

## First-use hints: implemented in 1.3.2-polish2

- [x] Restore 1.3.1 baseline and owner-supplied 1.3.2-polish1 gesture patch.
- [x] Restore complete MIPS toolchain; verify archive SHA-256 and compile candidate.
- [x] Replace the old shard hint with four contextual, theme-aware tips.
- [x] Retire tips through discovery; stop Automatic after five healthy starts.
- [x] Provide Help > First-use hints (Automatic/On/Off) and Show hints again.
- [x] Verify both side layouts, four-second expiry, interaction dismissal,
      discovery, persistence, save failure and Help controls with real LVGL.
- [x] Keep rendering inexpensive and stable payload/release tags unchanged.
- [ ] Owner device test and acceptance of 1.3.2-polish2.
- [ ] Upload the updated dev bundle using owner's GitHub credentials.
- [ ] Publish toolchain and candidate binary/source/checksum backups as durable
      development/toolchain release assets. These are not uploaded yet.

## Later: usability consistency pass

- [ ] Audit Back, close, Apply and immediate settings for predictable behaviour.
- [ ] Check readability with bright, dark, busy and missing artwork; long and
      multilingual titles; empty queues, unavailable actions and loading states.
- [ ] Standardize touch targets, pressed feedback, spacing and visual hierarchy.
      Avoid enlarging everything independently and crowding the round screen.
- [ ] Preserve list position when returning from a detail screen.
- [ ] Test the 260 ms centre-icon tap delay from the gesture candidate on device.
      Check that it still feels snappy.

## Quick Settings: 1.3.2-polish4 dev checkpoint

- [x] Approved inset Arc layout: eight 66px targets; mirror Left.
- [x] Current/Arc selector and shared eight-item selection/reordering editor.
- [x] Reserve inner radio positions, explicit state and hold-to-settings.
- [x] Keep music disc/rim geometry and inexpensive press feedback.
- [x] Both-side LVGL geometry/input/editor/state, hints and gesture checks.
- [x] Build matching device binary, synchronize dev payload and metadata.
- [x] Prepare dev bundle and updated handoff/release notes.
- [ ] Owner uploads dev bundle and tests/accepts candidate on Disc.
- [ ] Consider stable 1.3.2 release only after device approval.

## 1.3.2-polish5 follow-up

- [x] Current: fixed radio pills, equal action rows, gaps, single-line bold labels.
- [x] Both-side checks for every caption, including sparse/no-radio layouts.
- [x] Compile updated device binary and synchronize dev payload/metadata.
- [ ] Owner device test of Current and unchanged Arc.

## 1.3.2-polish6

- [x] Restore Current list curvature and centred bottom pill.
- [x] Widen Arc to concentric 128px radius; fixed radios preserved.
- [x] Both-side native geometry/text/state/editor checks and static MIPS build.
- [ ] Owner Disc test and acceptance.

## 1.3.2-polish7

- [x] Compact uniform Current pill width, equal gaps including below radios.
- [x] Inset Arc to 122px radius for more clearance from rim controls.
- [x] Native both-side checks, rebuilt payload and dev bundle.
- [ ] Owner Disc test and acceptance.

## 1.3.2-polish8

- [x] Protect Current QS and Modes targets beyond the edge-Back zone.
- [x] Shorter Modes caps; single-line labels and 56px rows preserved.
- [x] Both-side native checks and matching rebuilt dev payload.
- [ ] Owner Disc test of protection margin, readability and mode interaction.

## 1.3.2-polish9: responsive track changes

- [x] Optional sharp-cover cache; existing artwork cache remains compatible.
- [x] Worker-prepared pixels and Disco tint; publish before cache persistence.
- [x] Unchanged-art guards and foreground priority over new prewarm work.
- [x] Nonblocking play-history enqueue, sleeping worker and separate DB connection.
- [x] Song Info metadata refresh only while visible; pending artwork delivery at 30ms.
- [x] Native rapid skips/stale results/SD guards/SQLite contention and ASan/UBSan.
- [x] Compile matching static MIPS mq_ui, displaying 1.3.2-polish9.
- [ ] Owner tests same-album skips, shuffled albums, rapid skips, Info, immersive,
      Most/Recently Played and wake; report responsiveness/any audio interruptions.
- [ ] Measure device latency/battery impact; accept before committing/promoting.

## List layout follow-up (2026-10-12, preview only)

- QS List and Working Modes: continuous free-edge curve, shorter middle mode caps, 4px gutter beyond the 64px Back strip; mirrored for Left.
- Consistent 22px text per screen; mode label spacing -1px to retain readable Bluetooth DAC and the selected-state mark.
- Disco first in the theme picker; Progress Shape moved from Disco Colours to Disco Layout.
- Native dark/light and Right/Left previews checked. Await visual approval before device compilation or commit.
- The 1.4.0 freeze/crash investigation remains open; these layout edits do not fix the reported crashes.

## Replacement 1.4.0 release

- [x] Approved free-edge QS/Modes layouts, consistent fonts, theme order and Progress Shape move.
- [x] Asynchronous isolated EQ reads, worker-provided album colour, bounded artwork preparation and swipe cancellation.
- [x] Restore Battery gauge and keep Disc Shelf 1.0.4 / Album Roulette 1.3.1 separate.
- [ ] Owner device acceptance, collect persistent crash logs if automatic restarts recur.
- [ ] Owner publishes dev, then main and v1.4.0 after acceptance.
