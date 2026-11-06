# Development and release rules

- **dev:** experimental work and reviewed checkpoints.
- **main:** releases only, when the maintainer authorises.
- Use local branches/worktrees for larger work; preserve existing work before switching.
- No force pushes without explicit instruction; check the remote head first.
- A source commit doesn't imply a new binary or hardware validation.

Document the current implementation. Proposed features go in [targets](../todo.md). Builds and verification:
[workflow](DEV_WORKFLOW.md). Where things live: [source map](SOURCE_MAP.md).

A release includes source, matching payload, version, compiler/ABI, checksums, known limits and actual test results.
Never publish signing keys, device identifiers, FiiO firmware/rootfs or images containing it. Keep GPL/LGPL source
and notices intact. Old release notes stay as written.

Upstream is a reference for architecture, hardware evidence and process. Its features and deployment methods (themes,
settings, casting, update services, hot reload) aren't Disco features or safe Disco procedures by default.
