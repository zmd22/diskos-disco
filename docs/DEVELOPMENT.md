# Development and release rules

- **dev**: experimental work and reviewed checkpoints.
- **main**: release/publication work only when explicitly authorised.
- Larger work may use a local branch/worktree; preserve existing work before switching.
- Force updates require explicit instruction; compare the expected remote head before writing.
- A source commit does not imply a new firmware binary or hardware validation.

Document current implementation, not a remembered chat baseline. Keep proposed features in [targets](../todo.md). Use [the workflow](DEV_WORKFLOW.md) for builds and verification, and [the source map](SOURCE_MAP.md) to find authoritative implementations.

Release material includes source, matching payload, version, compiler/ABI, checksums, known limitations and actual test results. Never publish private signing keys, device identifiers, FiiO firmware/rootfs or generated stock-containing images. Preserve GPL/LGPL corresponding source and notices. Historical release notes remain historical.

Upstream is a reference for architecture, hardware evidence and contribution discipline. Its newer themes, settings, casting features, update services and hot-reload instructions are not automatically Disco features or safe Disco deployment procedures.
