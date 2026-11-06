# UI source and build

`mq_ui` is the native LVGL 9.2.2 interface. It drives the stock `mq_player` over POSIX message queues and doesn't
replace the audio engine. Target: 360×360 framebuffer, Ingenic X2000, static little-endian MIPS32r2/o32, FP64,
NaN2008.

Use the vendored LVGL and the pinned toolchain recipe in `ui/toolchain/mcm-config.mak`, not a generic mipsel
compiler or stock LVGL. Build, preview and deploy per [DEV_WORKFLOW.md](DEV_WORKFLOW.md). A clean build and ABI check
don't guarantee byte-identical output.

Components: [source map](SOURCE_MAP.md). Presentation rules: [theme contract](../themes.md). UI code is
GPL-3.0-or-later; keep the notices in `ui/licenses/` and the repo's licensing records.

## Frozen release files

The payload manifest hashes every tracked file under `ui/`, including `ui/README.md` and old dev notes, so those stay
unchanged to keep release binaries verifiable. `ui/README.md`'s version and deployment notes are historical; this
guide supersedes them. Never rewrite the recorded source hash to make a changed tree look like it built an old payload.
