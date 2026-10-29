> Historical material. Not current 3.2.1 instructions; see the current documentation index.

# Save point 07 — Null hold-to-scrub feedback

## This step

Null progress supports a long-hold time preview, enlarged playhead and horizontal drag, sharing a new themed time/total readout with the forthcoming Immersive control. Seeks send once on release, use the actual bar geometry and latched duration, and reject changed track identities. Lost presses, excessive vertical travel and screen changes cancel safely. Display disabled-status width was adjusted for the larger Null reading font.

## Cumulative versus stable 2.5.3

Common Display hierarchy and disabled explanations; retained saved preferences; fitted/static labels; conservative idle/visual polling reductions with playback, History and hardware keep-alive retained; updated tests/guides; shortened Roulette copy and larger Null list/Music fonts (all four font sets); plus hold-to-scrub Null feedback and cancellation checks. No public release or push.

## Checks

Real LVGL pointer test passes long hold, moving preview, one seek on release, no seek while dragging, changed-track rejection and screen-leave cancellation. Existing normal tap/drag seeking remains. Generated font assets were inspected in all four Null Music styles in checkpoint 06.

## Remaining

8. Optional Immersive circular duration/volume control, artwork following the finger without release snap, centralized feedback, lyric hold/refresh for duration and safe volume/input handling.
9. Run focused regressions, real layout/interaction renders and target build; preserve sources/checkpoints and test/rollback deliverables. Physical battery/stability/wake checks remain with the owner.
