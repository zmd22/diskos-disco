# Squash

Bold Music typography, rising launcher (with configurable shortcuts), fixed right-side navigation, eight-icon Quick
Settings, bottom volume arc. Tap track text for actions, double-tap for play/pause, swipe horizontally to skip.

**Display › Squash settings** covers Music appearance and Navigation:

- Backgrounds: Plain, Blur, Cover. Font is a separate preference (`simple_track_font`).
- Playback state and mode indicators are separate toggles.
- Gesture controls and QS layout are fixed, so unrelated Disco options are hidden.

Shares Wheel lists, letter navigation, round track menus and Immersive. Standby timing is in Battery & power, online
services in Connectivity. See [Settings](../DISPLAY_SETTINGS.md).

Implementation: `ui/simple_theme.c`, `ui/simple_theme.h`, `ui/disco_home.c`, shared screen modules. Config keys use
the legacy `simple_*` prefix; there's no separate "Simple" theme.
