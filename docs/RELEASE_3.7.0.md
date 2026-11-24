# 3.7.0 release record

Fresh static MIPS32r2/o32/FP64/NaN2008 build using GCC 11.2.0 / musl 1.2.6 / binutils 2.44 (the versions pinned in `ui/toolchain/mcm-config.mak`), with `-ffp-contract=off`. Built from a clean export of commit `757eea3e6d51922552e2643160a634765b47dce9` (version 3.7.0, empty test label); the exact tracked UI snapshot is recorded in `payload/mq_ui.build.json`.

Binary size: 8107832 bytes (3.6.1: 8099632).
Binary SHA-256: `f7f5bcad389c374df4f9b454f6c10e790eac9c62601dd7eb433d51ea8a577442`
Binary MD5: `7bd3aa4eb82d299abb4ebe85af06ec5b`

What changed from 3.6.1: Immersive edge shortcuts on the controls layer (hint arrows; tap or inward pull to Queue / Up Next and Search, pull down for Quick Settings; inactive while the controls are hidden; Home from those screens ends Immersive and frees its buffers); a side-edge back swipe on the modal keyboard (`kbinput_cancel`), giving Search a Back gesture in every theme; the USB DAC info screen reads the host's rate from the gadget capture stream's `hw_params` (#34, by @powahbeng); the in-process artwork decoder reads Ogg Opus / Vorbis embedded pictures with bounded packet and Base64 handling (#35, by @mean-ui-thread). `vendor/setup-macos.sh` checks the linked Homebrew keg (#34). No audio, Bluetooth, Wi-Fi or update-path code changed. The separate `payload/diskos-artdec` helper is unchanged; the UI decodes covers in process.

Validation: the ELF is static MIPS32r2/o32/FP64/NaN2008 (checked with readelf); release preflight checks source/version/ELF/binary checksums. Host: host-render build, a touch-driven Immersive edge test in all nine themes (pulls with the controls up and hidden, rim drags, arrow taps, lyrics toggle, Home), `artdec_test.py` plus real Opus/Vorbis covers and a mutation run, `uac_rate_test.py`. The same source labelled 3.7.0-test (before the keyboard back swipe) was signed and installed on a Disc; this exact binary has not itself been physically tested.

[Release notes](releases/v3.7.0.md).
