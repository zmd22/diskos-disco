> Historical material. Not current 3.2.1 instructions; see the current documentation index.

# Save point 06 — Roulette copy and larger Null text

Base: stable 2.5.3 (`9ba4ded`); follows tested 2.5.4-dev1 save point 05.

## This step

Roulette landing description shortened to “Rediscover an album.” Null list reading roles increase one size; Original, Jersey, Handjet and Matrix Music faces increase one step using generated font assets. Added baked Jersey 46, Pixelify 24, Handjet 42, VT323 30/33, pixel 18 and dot 40 faces. Library rows and Display cards gain enough height for larger reading text. Restyling recognizes already-themed fonts so it does not enlarge them twice. Music title/artist positions retain separation at the larger sizes.

## Cumulative versus 2.5.3

Common Display hierarchy, consistent theme controls, explanatory disabled settings, fitted/static setting labels, retained configuration keys/defaults, native tests and updated guides; reduced unused Null heartbeat/QS copies, offscreen visual polling and unchanged label updates. Playback/History, hardware keep-alive and existing animation timings retained. Added the Roulette copy and Null typography changes above. No migration/GIF feature, public release tag or push.

## Checks

Native renderer builds, theme-role and whitespace rules pass. Inspected actual LVGL Music renders for all four Null font choices. Fonts are generated once at build time, with existing fallback chains; no runtime bitmap scaling added. Seeking work is still pending.

## Remaining in this pass

7. Null long-hold progress scrubbing with visible time feedback, cancellation and track identity checks.
8. Optional Immersive circular duration/volume control with finger-driven artwork rotation, feedback and lyric holding during duration seeks.
9. Focused gesture/layout/renderer checks, development binary, cumulative documentation and recoverable source/test packages. Physical device testing remains necessary.
