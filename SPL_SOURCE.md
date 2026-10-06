# Stage-1 SPL - corresponding source & build (GPL-2.0)

The installer's USB stage-1 loader (`flash/disc_spl_lpddr3.bin`) that brings up DRAM before
the on-device NAND writer runs is **built from GPL source** - no vendor/USBCloner binary is
redistributed - **plus a small DDR parameter block (about 332 bytes of memory-chip register
settings) captured from the device's factory configuration**; see "DDR parameter block" below
for its provenance, which is an open item. This file is the GPL-2.0 "corresponding source" pointer + build recipe.

## Source

- **Upstream:** Ingenic-community **uboot-xburst**, U-Boot 2013.07, commit `1060b516` (GPL-2.0,
  `COPYING` in-tree). https://github.com/Ingenic-community/uboot-xburst
- **Our patches** (a small, self-contained delta adding W63AH6NKB LPDDR3 support to the X2000
  USB "burner" SPL, ported from the GPL tobunto/u-boot X2000 tree):
  1. `-fcommon` + a mask-ROM return shim in `arch/mips/cpu/xburst2/x2000/start.S` (DDR-only
     stage-1 returns to the mask ROM after DRAM init).
  2. `ddr_mr11` field + macro in `ddr_reg_data.h`.
  3. `case LPDDR3:` MR sequence (MR63,10,1,2,3,11) in `ddrc_dfi_init()`.
  4. Force `type = LPDDR3` in `sdram_init()` (this part's controller CFG.TYPE reads as LPDDR2).
  5. **PHY PLL divisors `FBDIV=8, PDIV=4`** for W63AH6NKB LPDDR3 @400 MHz (the tree's stock
     20/5 sets the wrong PHY clock and DDR training never converges).
  6. Bounded every hardware-poll (no infinite hang on a bad unit) + a small TCSM breadcrumb
     record for field diagnostics.
  7. **Robust watchdog disable** (clear `WDT_TCER.TCEN` then stop the WDT clock via `TCU_TSSR`)
     **and dropped the redundant dynamic-calibration sweep** - the stock code stopped only the WDT
     clock (leaving the counter armed, which reset the device ~36 min into a flash), and the sweep
     left a marginal PHY calibration. Device-verified: survives 25 min idle + full 76 MB DRAM
     round-trip intact (the 2026-08-25 flash-failure fix).

The complete patched source is shipped as `spl-src/uboot-xburst-lpddr3-src.tar.gz`, with the
delta over upstream also provided as a readable patch series in `spl-src/patches/`. Frozen at the
commit the released binary was built from: `7caf048` / tag `lpddr3-working-v2` (Ingenic-community/uboot-xburst `1060b516` + 8 patches).

## Toolchain

- `gcc-10-mipsel-linux-gnu` (10.5.0) + binutils for mipsel. (The tree's compiler-compat headers
  stop at gcc-10; newer GCC fails. Pin GCC 10.)

## Build + assemble

```sh
export ARCH=mips CROSS_COMPILE=mipsel-linux-gnu-      # gcc-10 on PATH as mipsel-linux-gnu-gcc
make distclean
make burner_x2000_lpddr2_config
make -j                                               # -> spl/u-boot-spl.bin
# USB stage-1 = DDR param block + zero-pad to 0x800 + spl/u-boot-spl.bin
#   load 0xb2401000, exec 0xb2401800; see flash/assemble_stage1.py
```

## DDR parameter block

The stage-1 prepends a 0x800-byte parameter region the SPL reads at `0xb2401000`:
a **BDIF** (board-info) record + a **DDR** record of 35 Winbond W63AH6NKB-BI register words
(controller/PHY/MR/timing values - hardware register settings for LPDDR3 @400 MHz, 24 MHz
EXTAL, 800 MHz PLL).

**Provenance (honest, and an OPEN item):** these values were **captured from the device's factory
configuration** (they match what the vendor SPL uses), NOT independently re-derived from the
W63AH6NKB datasheet. `flash/assemble_stage1.py` copies roughly **332 bytes** of this captured
block into the shipped stage-1 binary. These are low-level hardware register settings, but we do
**not** claim, as a legal conclusion, that "they are physical facts, therefore unrestricted." The
provenance of these vendor-origin bytes is an **unresolved compliance item**. Before any wide/public
binary distribution it should be resolved by one of: (a) independently regenerating the complete
parameter/BDIF block from documented datasheet values; (b) obtaining and documenting redistribution
permission; or (c) having counsel review and approve the provenance with an accurate disclosure of
the copied vendor-origin bytes. Until then the captured block ships as both the working block and a
regression oracle, and this limitation is disclosed here.

## Supported hardware

This stage-1 is built for the **Winbond W63AH6NKB-BI LPDDR3** DRAM and configures that chip.

**The Snowsky Disc uses this one DRAM chip.** The Disc's own stock SPL contains exactly one DRAM
config - `LPDDR3_W63AH6NKB-BI` - and initializes only that chip. Because a Disc must run FiiO's
stock firmware (whose SPL brings up only W63AH6NKB), any Disc in the field necessarily has this
chip; FiiO cannot ship a unit with DRAM its own bootloader can't initialize. So supporting
W63AH6NKB covers every current Disc, not "one of several." (The chip names `W97BV6MK` and
`NK6CL256M16` that appear in Ingenic's cloner configs are for *other* X2000-family boards -
x2000e/x2000h/m300 - not the Disc.)

**Residual / tail risk:** we have physically verified our own unit(s); "every Disc has W63AH6NKB"
is a strong inference from the stock SPL, not a survey of many units, so a hypothetical future
hardware revision with different DRAM cannot be ruled out. The SPL **fails safe** on DRAM it cannot
bring up: bounded polls return to the mask ROM instead of hanging, and a TCSM breadcrumb
@0xb24017c0 reports the failing stage - no brick, still mask-ROM-recoverable. Note the installer
does **not** run a separate DRAM verify-before-erase gate on each flash, so DRAM that *initialises
but is marginal* is a residual risk (a bad read-back would be written to NAND and would verify
against that same DRAM). A future revision could add a pre-erase DRAM round-trip and/or multi-chip
auto-detect if a different Disc DRAM ever turns up.

## Verification

Device-qualified over Ingenic mask-ROM USB boot **during development**: the stage-1 loads,
initialises LPDDR3, returns to the mask ROM, and a DRAM write/read round-trips byte-for-byte. This
was the bring-up qualification of the SPL itself; it is **not** a gate the installer re-runs on
every flash (see the residual-risk note above).
