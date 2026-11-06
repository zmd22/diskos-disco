# Design rules

Themes differ on purpose: there's no universal ring angle, font, accent, card radius or Music layout.

- Design at 360×360 with the round bezel visible.
- Keep text, controls and feedback clear of the bezel and theme navigation; check Disco's mirrored side.
- Use colour roles (`TC`) and font roles (`TF`); keep international fallback chains. Respect explicit component
  styling and `kit_keep` exceptions.
- Target ≥44×44 px effective touch areas; measure hit areas, not visuals. (Not all existing controls meet this.)
- Labels never scroll vertically: fit, wrap, truncate or scroll horizontally when selected. Lists and lyrics scroll by design.
- Preserve theme identity: artwork and translucency (Disco), pixel type and palettes (Null), bold rounded forms
  (Squash), typography and accent (Zune), and the existing Ring, Braun and Stone designs.
- Keep saved IDs, option indices and per-theme prefs stable. The "Ring" layout option is not the Ring theme.
- Keep animations and responsiveness. Optimise by reusing geometry and artwork, not by cutting animations.
- Loading, empty, disabled and error states need readable, actionable text; never rely on colour alone.
- Test light, dark and palette variants with long metadata and missing artwork. Host renders don't prove device
  performance or hardware safety.

Implementation: [theme contract](../themes.md), `ui/theme.c`, `ui/theme_kit.c`, `ui/curvelist.c`, theme modules.
Settings: [reference](DISPLAY_SETTINGS.md).
