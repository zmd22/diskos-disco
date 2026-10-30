# Build, verify and deliver

Work on `dev` against an identified source baseline. For this documentation, that is Disco 3.3.0 (tag `v3.3.0`). Check the actual branch, local changes and payload manifest before starting. Keep host rendering and MIPS object files in separate build trees.

## Target build

Use the pinned GCC 11.2.0 / musl 1.2.6 toolchain for static little-endian MIPS32r2, o32, FP64 and NaN2008. See [UI build instructions](UI.md). With a complete toolchain extracted locally:

```sh
python3 ui/tools/build_device.py \
  --toolchain /absolute/path/to/mipsel-n2008-musl-cross \
  --cache /absolute/path/to/mips-build-cache \
  --out /absolute/path/to/new-candidate-output --jobs 2
```

The helper builds mq_ui and helpers, checks ELF/ABI/static linkage and records hashes. It refuses to overwrite different output binaries. Its manifest is not interchangeable with the shipping `payload/mq_ui.build.json`; record payload provenance separately. Do not claim visual approval merely by passing a flag.

## Host checks

In a separate clean checkout/build tree:

```sh
make -C ui CROSS= host-render -j2
make -C ui check-theme
```

Choose focused diagnostics from `ui/tests/` for the changed behaviour. For settings, inspect `settings_alignment_test.py` and `display_pass_test.py`; for playback/power, the relevant idle, transport, Bluetooth and storage checks. Report actual results, including baseline failures. Do not state that all tests pass because the theme source gate passes.

Render changed screens at 360 × 360, including long text, empty states, supported appearances and navigation. Hardware playback, charging, standby, storage handoff and frame pacing still require the Disc.

## Deliver

Keep source, version and payload metadata consistent. `tools/check-release.py` checks version, source hash and ELF/payload constraints; `tools/package-release.py` invokes it before packaging. A documentation change under `ui/` can affect the existing source-hash recipe, even without changing executable behaviour: preserve the original release record and explain the distinction rather than inventing a rebuild.

Use [signed SD updates](UPDATING.md) for UI candidates on an install with the matching public key. Initial installation, boot-chain changes or root-key changes require the appropriate installer workflow. Never overwrite the active `/usr/data/mq_ui` hard link or force-kill the player/UI as a generic deployment procedure.

Before releasing, record source revision, compiler/ABI, binary hash and size, validation performed and device-test status. Obtain maintainer approval before committing when requested, and before updating main or tags. The release workflow is triggered by `v*` tags, not an ordinary main push.
