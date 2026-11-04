# SPDX-License-Identifier: MIT
# Copyright (c) 2026 diskOS contributors
"""Owner update keys and signed SD updates for the Linux GUI (the same steps the Windows app runs).

Install: make the keys (diskos-keys.sh) or use an existing keys folder; the installer then flashes with
<keys>/diskos-root.pub.pem as the OTA root. Update: sign an mq_ui with diskos-sign.sh and replace the
diskos-update folder at the top of the Disc's SD card - found through USB storage mode, or any card the user picks.
No Tk here, so it is testable without a display."""
import json
import os
import shutil
import subprocess
import tempfile
import time

INSTALL_KEY = "diskos-root.pub.pem"
SIGN_FILES = ("leaf.key", "leafpub.pem", "keyauthz", "keyauthz.sig")
UPDATE_FILES = ("mq_ui", "update.manifest", "update.manifest.sig", "leafpub.pem", "keyauthz", "keyauthz.sig")

# The Disc's USB storage mode (stock /usr/project/storage_config.sh; Disco uses the same gadget).
DISC_USB_IDS = {("0525", "a4a5")}
DISC_USB_SERIAL = "ingenic-storage"

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def default_keys_dir():
    """Where new keys go: ~/Documents/diskOS Keys (as on Windows), or ~/diskOS Keys without a Documents folder."""
    home = os.path.expanduser("~")
    docs = os.path.join(home, "Documents")
    return os.path.join(docs if os.path.isdir(docs) else home, "diskOS Keys")


def keys_status(folder):
    """(can_install, can_sign, missing_for_sign) for a keys folder."""
    if not folder or not os.path.isdir(folder):
        return False, False, list(SIGN_FILES)
    missing = [f for f in SIGN_FILES if not os.path.isfile(os.path.join(folder, f))]
    return os.path.isfile(os.path.join(folder, INSTALL_KEY)), not missing, missing


def tool(name):
    """diskos-keys.sh / diskos-sign.sh: from a source checkout (ui/tools/owner-keys) or a release folder (tools/)."""
    for d in (os.path.join(_ROOT, "ui", "tools", "owner-keys"), os.path.join(_ROOT, "tools")):
        p = os.path.join(d, name)
        if os.path.isfile(p):
            return p
    raise FileNotFoundError(f"{name} not found next to the installer")


def _run(cmd, log):
    """Run a command, streaming its output lines to log(line). Returns the exit code."""
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    for line in p.stdout:
        log(line.rstrip("\n"))
    return p.wait()


def make_keys(folder, log):
    """Make new keys into `folder` (created). Refuses to touch a folder that already holds keys."""
    if os.path.exists(os.path.join(folder, "root.key")) or os.path.exists(os.path.join(folder, "leaf.key")):
        raise RuntimeError(f"There are already keys in {folder}. Choose \"Use my keys folder\" instead.")
    tmp = tempfile.mkdtemp(prefix="diskos-keys.")
    try:
        out = os.path.join(tmp, "keys")
        if _run(["sh", tool("diskos-keys.sh"), out], log) != 0:
            raise RuntimeError("Making the keys failed (see the log).")
        os.makedirs(folder, exist_ok=True)
        os.chmod(folder, 0o700)
        for f in os.listdir(out):
            shutil.copy2(os.path.join(out, f), os.path.join(folder, f))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def update_label(mq_ui):
    """The package label: the version in mq_ui.build.json next to the file when it matches, else 'custom'."""
    meta = os.path.join(os.path.dirname(os.path.abspath(mq_ui)), "mq_ui.build.json")
    try:
        with open(meta) as f:
            v = str(json.load(f).get("version", ""))
    except (OSError, ValueError):
        return "custom"
    ok = v and len(v) <= 32 and all(c.isalnum() or c in "._-" for c in v)
    return v if ok else "custom"


def sign_update(mq_ui, keys, out_dir, log):
    """Sign mq_ui with the keys into out_dir (emptied and recreated by the script). Returns out_dir."""
    if _run(["sh", tool("diskos-sign.sh"), mq_ui, update_label(mq_ui), keys, out_dir], log) != 0:
        raise RuntimeError("Signing didn't work (see the log).")
    missing = [f for f in UPDATE_FILES if not os.path.isfile(os.path.join(out_dir, f))]
    if missing:
        raise RuntimeError("The signed update is incomplete: missing " + ", ".join(missing))
    return out_dir


# ---- finding the Disc ------------------------------------------------------------------------------------------------
def _unescape(s):
    """/proc/mounts escapes space, tab, newline and backslash as octal."""
    return s.replace("\\040", " ").replace("\\011", "\t").replace("\\012", "\n").replace("\\134", "\\")


def _usb_identity(dev, sys_root="/sys"):
    """(vid, pid, serial) of the USB device behind /dev/<dev> (a partition or a disk), or None."""
    name = os.path.basename(dev)
    p = os.path.join(sys_root, "class", "block", name)
    if not os.path.exists(p):
        return None
    p = os.path.realpath(p)
    while p and p != os.path.dirname(p):
        v = os.path.join(p, "idVendor")
        if os.path.isfile(v):
            def rd(f):
                try:
                    with open(os.path.join(p, f)) as fh:
                        return fh.read().strip().lower()
                except OSError:
                    return ""
            return rd("idVendor"), rd("idProduct"), rd("serial")
        p = os.path.dirname(p)
    return None


def find_cards(mounts="/proc/self/mounts", sys_root="/sys"):
    """Mounted USB volumes the user can write to: [{"path", "name", "disc"}], the Disc first.
    disc = the Disc in USB storage mode (its gadget's USB id/serial); other USB volumes (a card reader) are listed
    too, as the user's choice."""
    seen, cards = set(), []
    try:
        with open(mounts) as f:
            lines = f.read().splitlines()
    except OSError:
        return []
    for line in lines:
        parts = line.split()
        if len(parts) < 3 or not parts[0].startswith("/dev/"):
            continue
        dev, mnt = parts[0], _unescape(parts[1])
        if mnt in seen or mnt in ("/", "/boot", "/home") or mnt.startswith(("/boot/", "/sys", "/proc")):
            continue
        ident = _usb_identity(dev, sys_root)
        if ident is None or not os.access(mnt, os.W_OK):
            continue
        vid, pid, serial = ident
        disc = (vid, pid) in DISC_USB_IDS or serial == DISC_USB_SERIAL
        seen.add(mnt)
        cards.append({"path": mnt, "name": ("Disc (USB storage) - " if disc else "") + os.path.basename(mnt.rstrip("/")),
                      "disc": disc})
    cards.sort(key=lambda c: not c["disc"])
    return cards


def place_update(update_dir, card_root, log):
    """Copy the six files one by one into <card>/diskos-update (created if missing), overwriting the previous run's,
    then flush and read each back. Never copy/move the folder as a whole: on some cards deleting the old folder
    silently fails and the new one ends up nested inside it (docs/UPDATING.md, step 4). Overwriting all six by name
    replaces every file of the previous run, so two runs are never mixed."""
    dest = os.path.join(card_root, "diskos-update")
    os.makedirs(dest, exist_ok=True)
    for f in UPDATE_FILES:
        src, dst = os.path.join(update_dir, f), os.path.join(dest, f)
        with open(src, "rb") as a, open(dst, "wb") as b:
            shutil.copyfileobj(a, b)
            b.flush()
            os.fsync(b.fileno())
        log("Copied " + f)
    os.sync()
    for f in UPDATE_FILES:                                      # what is on the card now must be what was signed
        with open(os.path.join(update_dir, f), "rb") as a, open(os.path.join(dest, f), "rb") as b:
            if a.read() != b.read():
                raise RuntimeError(f"{f} on the card doesn't match the signed file. Copy it again.")
    log("Wrote " + dest)
    return dest


def keep_update(update_dir):
    """No card: keep the signed folder under ~/diskOS Updates/<time>/diskos-update for copying by hand."""
    base = os.path.join(os.path.dirname(default_keys_dir()), "diskOS Updates", time.strftime("%Y-%m-%d_%H%M%S"))
    dest = os.path.join(base, "diskos-update")
    shutil.copytree(update_dir, dest)
    return dest
