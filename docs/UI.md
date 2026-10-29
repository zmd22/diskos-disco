# UI source and build reference — 3.2.2

`mq_ui` is the native LVGL 9.2.2 interface. It talks to the stock `mq_player` over POSIX message queues; it does not replace the audio engine. The target is a 360 × 360 framebuffer on the Ingenic X2000, using static little-endian MIPS32r2/o32, FP64, NaN2008 binaries.

Use the vendored LVGL and pinned toolchain recipe in `ui/toolchain/mcm-config.mak`. Do not substitute a generic mipsel compiler or an unmodified upstream LVGL checkout. Follow [Build, verify and deliver](DEV_WORKFLOW.md) for isolated device builds, native previews and signed SD updates. Byte-identical rebuilds are not guaranteed by a successful build or ABI check.

See [the source map](SOURCE_MAP.md) for components and [theme authoring](../themes.md) for presentation rules. UI code is GPL-3.0-or-later; retain the component/font notices under `ui/licenses/` and the repository's licensing records.

## Frozen release snapshot

The shipped payload manifest hashes every tracked file under `ui/`, including its README and historical development notes. Those files remain unchanged to preserve verification of the original 3.2.1 binary. The old candidate/version and deployment prose in `ui/README.md` is historical; this guide supersedes it. Use the current guides under `docs/` for development. Do not replace the recorded source hash merely to make a changed source tree appear to have built the old payload.
