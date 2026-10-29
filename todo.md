# Development targets after Disco 3.2.1

Targets below are proposals or verification work, not shipped features. Work belongs on dev; changes, builds and releases follow maintainer direction.

## Current UI targets

- Make Shade follow the finger during the pull; 3.2.1 animates after the gesture.
- Extend artwork coverage in Wheel album/playlist lists; preserve smooth motion and bounded decoding work.
- Refresh native 360 × 360 screenshots after layout approval, including long labels, empty states and all supported appearances.
- Review secondary-screen theme consistency: Null Stats typography, file dialogs, Apps headers, Date & Time and Weather. These are audit proposals, not approved redesigns.

## Feature proposals

- Local song ratings and smart playlists using reliable listening data; not implemented as a 3.2.1 feature.
- Optional dyslexia-friendly text font with preserved icon/clock/non-Latin rendering; requires layout and device evaluation.

## Engineering validation

- Measure screen-off current, large-library memory, scrolling frame pacing and Bluetooth reconnect behaviour on hardware before claiming gains.
- Continue bounded/background database and artwork work where profiling identifies foreground stalls.
- Reconcile legacy minute-based signing epochs with timestamp-floor installations before changing the bundled signing tool; preserve monotonicity and signature/trial protections.
- Keep focused test harnesses aligned with production interfaces; distinguish test failures from device bugs.
- Revisit decoder cancellation only with a reproducible crash trace and device verification.

No dates, performance improvements or completion status are promised by this list. Previous checkpoint lists are available in Git history.
