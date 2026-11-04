# aptX BlueALSA (embedded in mq_ui)

Settings › Connectivity › **aptX (experimental)** writes these files to `/usr/data/diskos-aptx/` on the Disc (only
when they are missing or different) and uses them from the next Bluetooth start. Off (the default) leaves them
unused: BlueALSA is FiiO's stock one, exactly as in 3.3.1. They reach the Disc inside the signed `mq_ui`, so an SD
update brings them: no reflash.

| File | What | License |
|---|---|---|
| `bluealsa` | BlueALSA 4.0.0 (upstream) with `build/bluealsa-aptx/patches/` | MIT (`LICENSE-bluez-alsa.txt`) |
| `libfreeaptx.so.0` | libfreeaptx 0.1.1, the aptX / aptX HD encoder, unmodified | LGPL-2.1 (`LICENSE-libfreeaptx.txt`) |
| `libfreeaptx-0.1.1.tar.gz` | its exact corresponding source (not embedded) | LGPL-2.1 |

libfreeaptx stays a separate shared library next to `bluealsa` (found through RPATH `$ORIGIN`), so it can be replaced
with a modified build, as the LGPL asks. The Disc's own `libaptx`/`libaptxHD` are not used: they wrap FFmpeg, and
FiiO's FFmpeg has no aptX encoder (tested on the Disc, Nov 2026).

Rebuild (needs a copy of a V2.57 Disc's `/lib` and `/usr/lib`; vendor libraries stay out of the repository):

    SYSROOT=~/disc-sysroot-v257 sh build/bluealsa-aptx/build.sh ui/vendor/aptx-new
    # check, then copy bluealsa, libfreeaptx.so.0, the LICENSE files and the source tarball here and update SHA256SUMS

The build refuses a binary with the wrong floating-point ABI (the Disc is FP64), without `$ORIGIN`, or linked to the
Disc's libaptx.
