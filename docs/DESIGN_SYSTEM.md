# Disco design rules

Applies to the 3.3.0 UI. Themes deliberately differ; there is no universal ring angle, font family, accent, card radius or Music layout.

- Design and inspect at native 360 × 360 resolution with the round bezel visible.
- Keep primary text, controls and feedback clear of the bezel and each theme's navigation. Check Disco's mirrored side too.
- Use semantic colour roles (`TC`) and font roles (`TF`); retain international-text fallback chains. Respect explicit component styling and `kit_keep` exceptions.
- Aim for at least 44 × 44 px effective touch targets. Verify actual hit areas, not just visible shapes. This is a target, not a claim that every existing control meets it.
- Labels must not scroll vertically. Use fitting, wrapping, truncation or selected horizontal scrolling. Scrolling lists and lyric containers remain intentional interactions.
- Preserve theme identity: artwork and translucent surfaces in Disco; pixel typography/palettes in Null; bold rounded forms in Squash; typography and accent in Zune; existing Ring, Braun and Stone designs.
- Keep saved IDs, option indices and per-theme preferences stable. A layout option named Ring is not the Ring theme.
- Retain animation intent and responsiveness. Reuse geometry/artwork and avoid redundant work; do not remove animations to claim an optimisation.
- Loading, empty, disabled and failure states need readable, actionable text. Don't communicate state through colour alone.
- Theme appearance must be tested on representative light/dark/palette states, long metadata and missing artwork. Host renders do not establish device performance or complete hardware safety.

Implementation: [theme contract](../themes.md), `ui/theme.c`, `ui/theme_kit.c`, `ui/curvelist.c`, and theme-specific modules. Settings ownership: [settings reference](DISPLAY_SETTINGS.md).
