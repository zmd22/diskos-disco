#!/usr/bin/env python3
"""Focused review regressions through the production LVGL host renderer."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1]
subprocess.run(["make", "-s", "-C", str(app), "CROSS=", "host-render", "-j2"], check=True)
build_renderer = app / "tests/host_build/host-render"
fixture = app.parent / "docs/disco/modes.png"
count = 0
with tempfile.TemporaryDirectory(prefix="diskos-review-ui-") as temp:
    # Keep one immutable executable for the suite, even if the workspace is rebuilt.
    renderer = Path(temp) / "host-render"
    shutil.copy2(build_renderer, renderer)
    output = Path(temp) / "fixture.ppm"
    def run(preset, variant, page, extra):
        global count
        shutil.copyfile(fixture, output.with_suffix(".png"))
        subprocess.run([str(renderer), str(preset), str(variant), page, "0", str(output)],
                       env={**os.environ, **extra}, check=True, timeout=20)
        count += 1
    for variant in (0, 1):
        run(10, variant, "preview-modes", {"REVIEW_MODES": "1"})
        run(10, variant, "preview-home", {"REVIEW_MA": "1"})
        run(10, variant, "preview-home", {"REVIEW_NO_TRACK": "1"})
        run(10, variant, "preview-home", {"REVIEW_IMM_REOPEN": "1"})
        for style in (0, 1):
            for ms in (0, 100, 200, 240, 260, 320, 500):
                run(10, variant, "preview-home", {"REVIEW_IMM_EXIT": str(ms), "DISCO_IMMSTYLE": str(style)})
            run(10, variant, "preview-home", {"REVIEW_IMM_EXIT": "500", "REVIEW_ANIM_OFF": "1", "DISCO_IMMSTYLE": str(style)})
        for preset in (8, 9):
            run(preset, variant, "preview-np", {"REVIEW_STANDARD_EXIT": "1"})
print(f"PASS: {count} focused LVGL scenarios")
