#!/usr/bin/env python3
"""Braun 2.0's production LVGL controls, including physical brightness direction."""
import os
from pathlib import Path
import subprocess
import tempfile

ui = Path(__file__).resolve().parents[1]
subprocess.run(['make', '-s', '-C', str(ui), 'CROSS=', 'host-render', '-j1'], check=True)
config = ui / 'tests/host_build/diskos.conf'
saved = config.read_bytes() if config.exists() else None
try:
    with tempfile.TemporaryDirectory(prefix='braun2-test-') as tmp:
        for variant in (0, 1):
            for size in (0, 1, 2):
                config.unlink(missing_ok=True)
                env = {**os.environ, 'REVIEW_BRAUN2': '1', 'FONTSZ': str(size),
                       'CFG_INT': 'disco_hints=2', 'ZNP_PATH': str(ui.parent / 'docs/disco/modes.png')}
                result = subprocess.run([str(ui / 'tests/host_build/host-render'), '14', str(variant),
                                         'preview-home', '0', str(Path(tmp) / 'out.ppm')],
                                        env=env, capture_output=True, text=True, timeout=30)
                assert result.returncode == 0, result.stdout + result.stderr
                print(f'PASS Braun 2.0 appearance {variant}, text size {size}: controls, all catalogue glyphs, editor and isolation')
finally:
    if saved is None:
        config.unlink(missing_ok=True)
    else:
        config.write_bytes(saved)
