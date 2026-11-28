# 3.7.2 release record

Fresh static MIPS32r2/o32/FP64/NaN2008 build using GCC 11.2.0 / musl 1.2.6 / binutils 2.44 (the versions pinned in `ui/toolchain/mcm-config.mak`), with `-ffp-contract=off`. Built from a clean export of commit `e40a762cff103ef6fb9e00eb26418dfa9704c949` (version 3.7.2, empty test label); the exact tracked UI snapshot is recorded in `payload/mq_ui.build.json`.

Binary size: 8111948 bytes (3.7.1: 8107832).
Binary SHA-256: `9fc56e3c524b20128c0014888192c2253453c6d16ffbf484b5afe55c432714d0`
Binary MD5: `d379c20dcaf6e7f4d196c7f5cd20b24e`

What changed from 3.7.1: the screensaver shows the working mode (USB DAC with the host rate, BT DAC with the codec and the phone's AVRCP track, AirPlay, USB storage) in every theme; for the BT DAC track, Disco calls `Device1.ConnectProfile` for AVRCP (`0000110c`) once per connection and reads the phone's `MediaPlayer1` (#36, by @powahbeng). `main.c` treats `brightness == 0` as a player blank whatever `bl_power` reads, and sends one more power press when diskOS had already turned the screen off, so the panel and touch wake (#37, by @ericklv). The in-process cover decoder skips whitespace in and accepts missing padding on METADATA_BLOCK_PICTURE / COVERART base64 (other malformed values are still rejected) and follows an Ogg stream after an ID3v2 tag. No audio-path or update-path code changed. The separate `payload/diskos-artdec` helper is unchanged; the UI decodes covers in process.

Validation: the ELF is static MIPS32r2/o32/FP64/NaN2008; release preflight checks source/version/ELF/binary checksums. Host: `artdec_test.py` (8 tests, incl. the new base64 and ID3 cases) and the decoder on Opus files from opusenc and mutagen, built for the host and for MIPS under qemu. #36 and #37 were tested by their authors on their own Discs; this exact binary has not itself been physically tested.

[Release notes](releases/v3.7.2.md).
