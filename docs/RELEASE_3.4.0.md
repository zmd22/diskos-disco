# 3.4.0 release record

Fresh static MIPS32r2/o32/FP64/NaN2008 build using pinned GCC 11.2.0 / musl 1.2.6, with `-ffp-contract=off`. Built from dev commit `7849f8fa2d0d194f2475092a6209ed4724c37ad4` (version 3.4.0, empty test label); the exact tracked UI snapshot is recorded in `payload/mq_ui.build.json`.

Binary size: 7548060 bytes.
Binary SHA-256: `559afd09a05bd0c89c4fa5167753e26e19839a2129f5278b39331b69eca692ea`
Binary MD5: `d422bdbc4ab473cf68e61d33c396168e`

Embedded (written to the Disc only with Settings › Connectivity › aptX (experimental) on): BlueALSA 4.0.0 (MIT) with `build/bluealsa-aptx/patches/0001`-`0003`, and libfreeaptx 0.1.1 (LGPL-2.1-or-later) as a separate shared library; both in `ui/vendor/aptx/` with the libfreeaptx source.

Validation: target build verified static linkage and the MIPS32r2/o32/FP64/NaN2008 ABI; release preflight checks source/version/ELF/binary checksums; complete-ZIP packaging checks document links. Host tests cover codec choice, BlueALSA selection and switching, the crash guard and Quiet Wi-Fi for Bluetooth. The same source was tested on a Disc as signed test builds 3.4.0-test7 to test9 (LDAC on a FiiO EH11, aptX on a Sennheiser MTW4, switching between them, Quiet Wi-Fi); this exact binary differs only in its version and codec choice labels and has not itself been physically tested.

[Release notes](releases/v3.4.0.md).
