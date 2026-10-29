> Historical material. Not current 3.2.1 instructions; see the current documentation index.

# Save point 05 — validated development candidate

Baseline: 2.5.3 `9ba4dedfbba9d4a0ed4421cba10c3c4800f48818`. Compiled source commit: `0d2ce3c7c6602d024fa5e974e7fe5acf4c0191a6`. Candidate: **2.5.4-dev1**.

## This step

Restored checkpoint work after the workspace reverted; the replacement commits explicitly document that reconstruction. Updated Help/hints regression paths and corrected the Squash test to use its existing namespaced hint preference. Added a repeatable native test driver, current Display guide, updated tour paths and physical-device test instructions. Built an isolated MIPS test binary with the repository's prebuilt GCC 11.2.0 toolchain. Prepared raw/test/rollback assets, provenance and recoverable Git source/checkpoint bundle. Shipping payload and public release remain 2.5.3.

## Cumulative changes versus 2.5.3

1. Common Display order across themes: Brightness, Theme, Appearance, Music screen, Standby, Navigation, Layout & extras.
2. Short Music Text/Information/Progress/Immersive pages; shared navigation/QS/gesture pages; separate online/cache page. Original setting keys, defaults, option arrays and handlers retained. Theme-scoped values are preserved.
3. Unsupported settings stay dimmed, explain why on tap and reject writes/direct detail entry. Ring/Braun's independent Now Playing-style control remains available.
4. Short fitting headings and static wrapping Display labels replace label marquees.
5. Null visual heartbeat pauses when unused; unnecessary QS player-state copies removed. Visual timers skip the fully-off panel. Clock catches up on wake. Settings/scan labels avoid unchanged text updates.
6. Native coverage and documentation for the new paths. Development version identifies the test binary.

Playback, History, animation timings, artwork quality and the battery/status hardware keep-alive retain their existing behavior. No GIF feature or migration/reset machinery. No remote push or release tag.

## Verification

- Native Display/idle suite passed: all five themes, twelve page/back destinations, disabled taps/direct guards, preference preservation, theme-specific editable keys, Null timer lifecycle and offscreen state-read suppression, navigation/QS/gestures/volume, mode layouts, hints and theme picker.
- Additional Null pointer suite passed for the other two palettes; all three palettes covered in total.
- History regression, theme compatibility, theme-role rules, whitespace and unchanged configuration-table definitions passed.
- Real LVGL renders inspected for Disco, Null Matrix and Squash Display/Appearance/Music pages.
- Tracked UI files in the isolated MIPS build match the compiled source commit byte-for-byte.
- ABI verified: static little-endian MIPS32r2, o32, FP64 hard-float, NaN2008; stripped.
- Binary size: **5,743,952 bytes**, versus 2.5.3 **5,737,900 bytes** (+6,052 bytes). This pass does not claim a size reduction.
- Binary SHA-256: `7c9d652606c6efdfaa9ac6fed9cb5348f39ae2a56565be8d2564718fd5af625d`.
- Builds encountered incomplete/empty object outputs; removed incomplete outputs only after the build ended and rebuilt. The final links succeeded. No compiler/toolchain changes introduced.

## Remaining planned work in this pass

Implementation and off-device validation are complete. Physical testing remains: repeated wake/lock cycles, longer playback, offscreen History recording, memory over repeated navigation, and matched battery comparisons against 2.5.3. No measured battery or CPU gain is claimed. Review device feedback before promotion to a normal release. Further runtime changes should be driven by that evidence, each with a new checkpoint.
