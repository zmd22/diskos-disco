#!/usr/bin/env python3
"""Compatibility entry point: recovery EQ policy and async-save coverage moved here."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).with_name('stability_test.py')),run_name='__main__')
