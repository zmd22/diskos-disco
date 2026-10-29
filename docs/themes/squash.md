# Squash in Disco 3.2.1

Squash has bold Music typography, a rising launcher, fixed right-side navigation, eight-icon Quick Settings and a bottom volume arc. Single-tap track text opens actions; double-tap controls playback; horizontal swipes skip. The launcher supports configured shortcuts.

Display > Squash settings owns Music appearance and theme Navigation. Music backgrounds are Plain, Blur and Cover; the font preference is independent (`simple_track_font`). Playback state and playback mode indicators are separate switches. Its Music gesture controls and QS geometry are fixed, so unrelated Disco switches are hidden.

Shared features include Wheel lists, letter navigation, round track menus and Immersive. Configure standby timing in Battery & power and online services in Connectivity. See [Settings](../DISPLAY_SETTINGS.md).

Implementation: `ui/simple_theme.c`, `ui/disco_home.c`, `ui/simple_theme.h` and shared screen modules. Compatibility keys still use `simple_*`; this is not a separate theme named Simple.
