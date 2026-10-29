> Historical material. Not current 3.2.1 instructions; see the current documentation index.

# Save point 03 — layout and navigation checks

## This step

Native LVGL tests cover all five themes, twelve page/back destinations, disabled taps/direct entries, preference preservation, theme-specific writable track-number routing and Ring/Braun's separate Now Playing style. Headings shortened to fit Squash. Display names wrap statically; disabled status uses smaller secondary text to avoid crowding.

## Cumulative versus 2.5.3

Checkpoint 02 common hierarchy/capability guards plus static Display labels and native regression tests. Existing key/default/handler definitions retained. Runtime work and animations unchanged at this stage.

## Checks

Recorded session passed Display tests for all five themes and inspected actual LVGL renders for Disco/Null/Squash. Tests will be rerun after reconstruction; prior results do not certify these restored bytes.

## Remaining

4. Restore conservative visual-work reductions and validate. 5. Re-run focused checks, compile test binary, preserve source bundle and document device-only tests. Original be0afb1 was lost with the workspace snapshot; reconstructed here from the chat record.
