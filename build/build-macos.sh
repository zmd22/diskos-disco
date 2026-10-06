#!/bin/bash
# build-macos.sh - build the standalone diskos-installer app on macOS.
# RUN ON A MAC. Produces build/dist/diskos-installer for this Mac's arch.
#
# Intel + Apple Silicon coverage (pick one):
#   * Simplest single file: build x86_64 (below, on an Intel Mac OR via an
#     x86_64 Homebrew under Rosetta) - it runs natively on Intel and under
#     Rosetta 2 on M-series. Needs Rosetta on M-series (macOS offers to install it).
#   * Best native: build on each arch -> two binaries (diskos-installer-arm64 /
#     -x86_64). Run this script on an Intel Mac and an Apple Silicon Mac.
#   * Universal2: build both arches, lipo the vendor tools + use PyInstaller
#     target_arch=universal2 (advanced; see build/NATIVE_TOOLS.md).
set -euo pipefail
cd "$(dirname "$0")/.."                        # installer/

[ "$(uname)" = "Darwin" ] || { echo "run this on macOS." >&2; exit 2; }

if [ "${1:-}" != "--skip-setup" ]; then
  echo ">> populating vendor/ for this Mac"
  bash vendor/setup-macos.sh
fi

VENV=build/venv
[ -d "$VENV" ] || python3 -m venv "$VENV"
"$VENV/bin/pip" install --quiet --upgrade pip pyinstaller pyusb pycryptodome

"$VENV/bin/pyinstaller" --clean --noconfirm \
  --distpath build/dist --workpath build/work \
  build/diskos-installer.spec

APP=build/dist/diskos-installer
# ad-hoc sign so it runs locally without a Developer ID (enthusiast tool).
# For distribution you'd sign + notarize with a real identity.
codesign --force --deep --sign - "$APP" 2>/dev/null || \
  echo "   (codesign ad-hoc skipped/failed - 'xattr -dr com.apple.quarantine $APP' if Gatekeeper blocks it)"

echo
echo "Built: $APP ($(uname -m))"
echo "Smoke test: $APP doctor"
echo "If Gatekeeper blocks it: xattr -dr com.apple.quarantine $APP"
