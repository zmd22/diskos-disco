# Audiobook playback speed

**Not planned.** Investigated 2026-10-29 on stock V2.57; nothing built. Live 1.25×–2× speed isn't possible today:

## Why not

- `mq_ui` is UI only; FiiO's closed `mq_player` does decoding and output via IPC ([command map](COMMAND_MAP.md)). No known command sets speed.
- `mq_player` (4,601,368 bytes) links `libavcodec`, `libavformat`, `libavutil`, `libswresample` and `libfiio_decoder`, but **not `libavfilter`** (home of `atempo`); neither does `libfiio_decoder.so`.
- `mq_player`'s "speed" strings are unrelated:
  - "Failed to set speed" is the SPI driver (`/dev/spidev0.0`).
  - `TransportPlaySpeed` / `CurrentSpeed` are the DLNA/UPnP renderer (1× only).

## What the Disc has

- `/usr/bin/ffmpeg` and `/usr/lib/libavfilter.so.7.57.100`, with `atempo` (pitch-preserving).
- One XBurst core (~2387 BogoMIPS), ~26 MB free RAM with UI and player running.

## Options

1. **Pre-process on a computer** (most practical):
   `ffmpeg -i book.m4b -map 0:a -map_chapters 0 -filter:a atempo=1.5 -c:a aac -b:a 64k "book (1.5x).m4b"`.
   Depending on FFmpeg version, chapter times may need dividing by the factor. Each copy costs SD space and has its own resume point.
2. **Pre-process on the Disc** with its `ffmpeg` while charging. Unbenchmarked; AAC encoding a long book likely takes hours. Time 60 s of audio first.
3. **Own decode path** (decode → `atempo` → output), bypassing `mq_player`. Real live speed, but requires taking over routing, Bluetooth and FiiO's sound settings; CPU headroom unknown. Large and risky.
4. **Raise the output sample rate.** Cheap but shifts pitch. Rejected.

A hidden tempo command in `mq_player` would have been the shortcut, but no tempo filter is linked, so that's closed unless FiiO adds one.
