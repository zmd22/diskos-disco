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

## Progressive lyrics

**Settings > Display > Text & layout > Progressive lyrics** enables smooth line movement and word highlighting
in Lyrics and Immersive. It is on by default. Turning it off restores static, whole-line highlighting. The separate
Screen transitions setting disables movement without removing word highlights. Tap a visible timed line to seek
to its timestamp; swipes and long presses do not seek. Seeking while paused keeps playback paused.

**Estimate word timing**, also on by default, adds an approximate word sweep to line-only LRC lyrics when
Progressive lyrics is enabled. Words share the interval until the next later line timestamp, weighted by their
character lengths. This does not analyze audio and can differ from the singer's timing. Blank timed lines mark
pauses. The final line uses the known track end; without one, it keeps whole-line highlighting. Changing either
setting refreshes loaded lyrics without changing files or downloading them again.

Exact Enhanced LRC timestamps always take priority over estimates:

```text
[00:01.00]<00:01.00>Follow <00:02.00>the <00:02.50>light<00:04.00>
[00:05.00]<00:05.00>Keep <00:06.00>moving<00:08.00>
```

The next inline timestamp ends a word or phrase. Without a terminal timestamp, the final exact span highlights
at its start. Malformed timings, unsupported layout and capacity limits fall back to whole-line highlighting.
Immersive supports 24 spans per line; truncated text or unusable exact timing never receives guessed replacements.
Untimed lyrics remain plain text. Existing local, embedded and optional online lyric sources are retained.

Glyph geometry is cached on line changes, and Immersive reuses text snapshots. Between line changes, frames update
positions and highlight clips without allocating or laying out text. Estimates use separate bounded scratch storage, leaving
parsed exact timestamps unchanged. Focused checks (host-render requires the native Linux build dependencies):

```sh
python3 ui/tests/lyrics_test.py
python3 ui/tests/lyrics_word_test.py
python3 ui/tests/imm_lyrics_compositor_test.py
make -C ui CROSS= host-render
python3 ui/tests/lyrics_motion_test.py --out /tmp/disco-lyrics-frames
make -C ui check-theme
```
