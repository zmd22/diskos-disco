# Source map and upstream comparison

## Baselines

This documentation describes **Disco 3.2.1**, release commit `91a7151720d55583c766f7ebd2047ba6620921e1`. Its shipped mq_ui SHA-256 is `597d8f2e4b881cf590392481a17b4eea385fef0e9ea25d74074a749e3b9731bb`.

The later main merge `bb389f6` changes Apps, Search and Settings for Zune; those changes are not in the 3.2.1 payload and are not described as shipped here.

Reference: [b0hemia/diskOS](https://github.com/b0hemia/diskOS/tree/53a9cc512d7aec766fd7c5e304e78cac6ca9b7d3), commit `53a9cc512d7aec766fd7c5e304e78cac6ca9b7d3`. Upstream's tour, documentation index, contribution rules and developer workflow informed the organisation, not Disco's feature list.

## Implementation owners

Paths below are relative to the repository root. Read the implementation together with its configuration and routes; a function or old preset remaining in source does not mean the user can select it.

| Area | Authoritative sources |
|---|---|
| Startup, power, input and screen lifetime | `ui/main.c`, `ui/screenmgr.c`, `ui/screens.h`, `ui/controls.c`, `ui/battery.c` |
| Playback IPC and persistence | `ui/ipc.c`, `ui/config.c`, `ui/musicdb.c` |
| Music, Immersive and track actions | `ui/ui.c`, `ui/songinfo.c`, `ui/npmenus.c`, `ui/disco_home.c`, `ui/null_theme.c` |
| Navigation, Quick Settings and Shade | `ui/disco.c`, `ui/home.c`, `ui/quicksettings.c`, `ui/screenmgr.c` |
| Library, albums, search and books | `ui/library.c`, `ui/albumwall.c`, `ui/search.c`, `ui/books.c`, `ui/playlistview.c`, `ui/scanner.c`, `ui/azjump.c` |
| Settings hierarchy and visibility | `ui/settings.c`, `ui/settings_display.h`, `ui/settings_order.h` |
| Theme definitions and styling | `ui/theme.c`, `ui/theme_defs.c`, `ui/theme_kit.c`, `ui/theme_kits.c`, theme-specific modules and generated fonts |
| Standby and overlays | `ui/saver.c`, theme saver modules, `ui/toast.c`, `ui/onboarding.c` |
| Network services | `ui/bt.c`, `ui/wifi.c`, `ui/weather.c`, `ui/lyrics.c`, `ui/lastfm.c`, Sendspin modules |
| Built-in apps | `ui/apps.c`, `ui/appshelf.c`, `ui/discshelf/`, `ui/boomshine/`, SongSphere and Roulette modules |
| Artwork and caches | `ui/art.c`, `ui/artcache.c`, `ui/artdec_inproc.c` and helper/tool sources |
| Initial installation | `diskos_installer/`, `install.sh`, `src/launcher/`, `src/usbboot/`, `flash/`, `windows/` |
| Boot selection and signed updates | `payload/` boot/verification scripts, `diskos_installer/imagebuild.py`, `ui/tools/owner-keys/` |
| Build and packaging | `ui/Makefile`, `ui/tools/build_device.py`, `build/`, `tools/check-release.py`, `tools/package-release.py`, `.github/workflows/` |
| Verification | `tests/`, `ui/tests/`; tests and fixtures are not runtime features |
| Third-party and legal source | `ui/lvgl/`, bundled libraries, `vendor/`, `corresponding-source/`, `spl-src/`, `licenses/` and notices |

## What carries over from upstream

The stock-player boundary, hardware evidence, installer architecture, fail-closed update verification, component licences and requirement for real-device validation remain relevant. Keep host renders, static/ABI checks and physical-device results distinct.

Disco owns its theme selection, settings hierarchy, Wheel lists, Shade, track menus and integrated apps. Do not copy upstream theme lists, Beam/casting screens, online-update availability or development hot-reload commands into these guides without an implementation check. Disco's documented UI deployment route is the signed SD update.

## Scope and evidence

The audit covers the first-party runtime, configuration/routing, app integration, installer and build areas needed to reconcile the guides. Vendored code, generated glyph data and old research are dependency/evidence records, not a fresh line-by-line correctness or security audit. No physical-device validation was performed for this documentation change.

Current user instructions live in this directory. Earlier plans and investigations are under [archive](../archive/README.md); release notes retain their historical meaning. The [UI source guide](UI.md) explains why documentation embedded in the hashed release source remains frozen.
