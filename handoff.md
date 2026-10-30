# Handoff — documentation baseline Disco 3.2.2

Release source: the commit tagged `v3.2.2`. Payload SHA-256: `0c84a91c4272bccaa5e4858867b23b557fbcaa92e259816d4da69bf28a5a7b99`.

Work on dev unless explicitly directed otherwise. Verify remote heads and local changes; later main commits are not automatically part of the 3.2.2 binary. The documentation cleanup is prepared for review and must not be committed before the requested summary/approval.

Read [docs](docs/README.md), [workflow](docs/DEV_WORKFLOW.md), [source map](docs/SOURCE_MAP.md) and [targets](todo.md). Preserve theme identities, saved setting IDs, audio behaviour and signed-update protections. The Ring theme and Ring Now Playing layout are separate.

The release records a static MIPS build; do not infer new device-test results from documentation edits. No firmware behaviour is changed by this cleanup.
