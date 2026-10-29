# Zune in Disco 3.2.2

Zune provides a typographic main menu, its own Music and Quick Settings presentation, accent volume controls, album views and lock-screen choices. Its original theme is credited to [namastecoconut-imagined](https://github.com/namastecoconut-imagined/diskos-disco-zune).

Display > Light / dark supports Dark, Light and Auto. Zune settings owns applicable accent, navigation and saver controls; Album Art/Clock Only and clock tint are theme-specific. Zune does not expose a separate Now Playing style selector. Shared Immersive lives directly under Display.

3.2.2 includes the Zune fixes from pull request #22 (search result text, empty rows while scrolling the theme list, duplicate Apps shortcuts). Do not describe later commits as part of a shipped payload simply because the version string is unchanged. See [source baseline](../SOURCE_MAP.md).

Implementation: `ui/zune_*.c`, `ui/home_shortcuts.c`, `ui/settings.c`, `ui/settings_display.h` and shared Immersive code.
