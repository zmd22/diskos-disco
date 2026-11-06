# Build, verify and deliver

Work on `dev`. Before starting, check the branch, local changes and payload manifest. Keep host-render and MIPS
objects in separate build trees.

## Device build

Pinned toolchain: GCC 11.2.0 / musl 1.2.6, static little-endian MIPS32r2, o32, FP64, NaN2008 ([UI build](UI.md)).

```sh
python3 ui/tools/build_device.py \
  --toolchain /absolute/path/to/mipsel-n2008-musl-cross \
  --cache /absolute/path/to/mips-build-cache \
  --out /absolute/path/to/new-candidate-output --jobs 2
```

Builds `mq_ui` and helpers, checks ELF/ABI/static linkage and records hashes; refuses to overwrite differing
binaries. Its manifest is not the shipping `payload/mq_ui.build.json`; record payload provenance separately. Passing
a flag is not visual approval.

## Host checks

In a separate clean tree:

```sh
make -C ui CROSS= host-render -j2
make -C ui check-theme
```

Run the `ui/tests/` diagnostics relevant to your change (settings: `settings_alignment_test.py`,
`display_pass_test.py`; playback/power: idle, transport, Bluetooth, storage). Report real results, including
pre-existing failures; `check-theme` passing doesn't mean everything passes.

Render changed screens at 360×360 with long text, empty states, every appearance and navigation. Playback, charging,
standby, storage handoff and frame pacing need the real Disc.

## Delivery

Keep source, version and payload metadata in sync. `tools/check-release.py` validates version, source hash and
ELF/payload constraints; `tools/package-release.py` runs it before packaging. Any change under `ui/`, docs included,
changes the source hash: keep the original release record and explain the difference rather than faking a rebuild.

Test UI candidates via [signed SD updates](UPDATING.md). First installs, boot-chain and root-key changes need the
installer. Never overwrite the live `/usr/data/mq_ui` hard link or kill the player/UI as a deployment method.

Before a release, record source revision, compiler/ABI, binary hash and size, validation done and device-test status.
Get maintainer approval before touching main or tags. The release workflow runs on `v*` tags and on main pushes
changing `ui/version.h` or `payload/mq_ui.build.json`, so have a verified matching payload before merging those.
