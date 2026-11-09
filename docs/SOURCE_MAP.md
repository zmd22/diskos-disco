# Source map and upstream comparison

## Baselines

- **Disco 3.4.4**, tag `v3.4.4`. Shipped `mq_ui` SHA-256: `788f12f01d0c8d9f431fe59b02f1709772e841cfcf66212f9c4f087237c66230` ([record](RELEASE_3.4.4.md)).
- Upstream reference: [b0hemia/diskOS @ `53a9cc5`](https://github.com/b0hemia/diskOS/tree/53a9cc512d7aec766fd7c5e304e78cac6ca9b7d3). Its docs shaped this documentation's structure, not Disco's feature set.

## Implementation owners

Paths are relative to the repo root. Read code together with its config and routing: dead functions or old presets in source aren't necessarily reachable.

| Area | Sources |
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
| Verification | `tests/`, `ui/tests/` |
| Third-party and legal source | `ui/lvgl/`, bundled libraries, `vendor/`, `corresponding-source/`, `spl-src/`, `licenses/` and notices |

## Relation to upstream

Shared with upstream: the stock-player boundary, hardware evidence, installer architecture, fail-closed update verification, licensing and the real-device testing requirement. Keep host renders, static/ABI checks and device results separate.

Disco-specific: theme selection, settings hierarchy, Wheel lists, Shade, track menus, integrated apps. Don't document upstream themes, Beam/casting, online updates or hot reload without checking the implementation. UI deployment is via signed SD update only.

## Scope

This map covers first-party runtime, config/routing, apps, installer and build. Vendored code, generated glyphs and old research weren't re-audited. Current docs live here; old plans are in [archive](../archive/README.md). Why docs inside `ui/` are frozen: [UI.md](UI.md).
