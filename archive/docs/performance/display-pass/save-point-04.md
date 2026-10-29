> Historical material. Not current 3.2.1 instructions; see the current documentation index.

# Save point 04 — reduce unnecessary visual work

## This step

Null heartbeat pauses outside Home/QS/open menu, resumes on entry/menu opening, and stops copying unused player state on QS. Visual heartbeat/charging, Disco progress/autofade and Squash QS skip fully-off panels. Periodic clock paint skips screen-off; wake refreshes it immediately. Settings/scan labels avoid unchanged text updates. Battery/status hardware keep-alive remains active; corrected misleading comment. Existing configuration persistence already avoids redundant writes while retaining failed-save retries.

## Cumulative versus 2.5.3

Shared Display hierarchy, original values/defaults/handlers retained, consistent theme controls, explanatory disabled states, fitting/static row text, native tests, plus visual-work reductions above. Playback, History, animation timings and artwork quality retained. No release or push.

## Validation

Recorded pre-reset tests passed all-five-theme Display navigation/preferences; Null timer pause/resume and screen-off state-read suppression; Null real-pointer navigation, QS, gestures and volume; Null/Squash mode layouts; Disco navigation, hints and theme picker; accents; History and theme compatibility. Restored bytes require a fresh final run. Physical CPU/battery/wake measurements remain pending.

## Remaining

5. Revalidate restored changes; review remaining capability details, update paths/docs, compile MIPS test binary, verify ABI/provenance, preserve bundle/checkpoints and supply test guide. No battery gain claim until device measurements. Original eaad50f was lost after workspace reset; this reconstructs its changes from the recorded session.
