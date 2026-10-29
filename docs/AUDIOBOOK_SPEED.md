# Audiobook playback speed

**Status: not planned.** Investigated 29 Oct 2026 on a Disc running the stock V2.57 player. Nothing was built.

People ask for 1.25× / 1.5× / 2× audiobook speed. Disco can't offer it as a live control, for these reasons.

## Why there is no speed control

- Disco (`mq_ui`) only draws the interface. FiiO's closed-source `mq_player` decodes and plays the audio, driven by IPC commands ([command map](COMMAND_MAP.md)). No known command changes playback speed.
- `mq_player` (4,601,368 bytes) links `libavcodec`, `libavformat`, `libavutil`, `libswresample` and `libfiio_decoder`, but **not `libavfilter`**. That is where FFmpeg's tempo filter (`atempo`) lives. `libfiio_decoder.so` doesn't link it either.
- The "speed" strings in `mq_player` are unrelated:
  - "Failed to set speed" belongs to the SPI driver (`/dev/spidev0.0`).
  - `TransportPlaySpeed` / `CurrentSpeed` belong to the DLNA/UPnP renderer, which plays at normal speed only.

## What the Disc does have

- `/usr/bin/ffmpeg` and `/usr/lib/libavfilter.so.7.57.100`, which includes `atempo` (changes speed while keeping pitch).
- A single Ingenic XBurst core (BogoMIPS ~2387) and about 26 MB of free RAM with the UI and player running.

## Options, if anyone picks this up

1. **Sped-up copies on a computer** (the most practical). For example:
   `ffmpeg -i book.m4b -map 0:a -map_chapters 0 -filter:a atempo=1.5 -c:a aac -b:a 64k "book (1.5x).m4b"`.
   Chapter times need dividing by the speed, depending on the FFmpeg version. Each copy uses its own space on the SD card and keeps its own resume position.
2. **Sped-up copies on the Disc** using its own `ffmpeg`, run in the background while charging. Unmeasured: AAC re-encoding on this CPU is likely to take hours for a long book. Benchmark about 60 s of audio first.
3. **Disco decodes books itself** (decode → `atempo` → output), bypassing `mq_player`. This would give real live speed control, but it means taking over output routing, Bluetooth and FiiO's sound settings, and the CPU headroom is unknown. Large and risky.
4. **Higher output sample rate.** Cheap, but it raises the pitch (chipmunk voices). Rejected.

A hidden tempo command in `mq_player` was the hoped-for shortcut. The binary has no tempo filter linked in, so that route is closed unless a future FiiO firmware adds one.
