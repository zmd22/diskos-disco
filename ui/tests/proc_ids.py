"""Translate syscall PIDs when the host mounts proc from an outer PID namespace.

Only processes in our own namespace qualify; no production supervision is changed.
"""
import os
from pathlib import Path

PROC = Path('/proc')
MATCHED = int(os.readlink('/proc/self')) == os.getpid()

def inner_id(directory):
    try:
        if os.readlink(directory / 'ns/pid') != os.readlink('/proc/self/ns/pid'):
            return None
        for line in (directory / 'status').read_text().splitlines():
            if line.startswith('NSpid:'):
                return int(line.split()[-1])
    except (FileNotFoundError, ProcessLookupError, PermissionError):
        pass
    return None

def proc_dir(pid):
    if MATCHED:
        return PROC / str(pid)
    for directory in PROC.iterdir():
        if directory.name.isdigit() and inner_id(directory) == pid:
            return directory
    raise FileNotFoundError(f'No proc entry for namespace PID {pid}')

def parent_id(outer_pid):
    return outer_pid if MATCHED else inner_id(PROC / str(outer_pid))

def processes():
    for directory in PROC.iterdir():
        if directory.name.isdigit():
            pid = int(directory.name) if MATCHED else inner_id(directory)
            if pid is not None:
                yield pid
