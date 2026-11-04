# SPDX-License-Identifier: MIT
# Copyright (c) 2026 diskOS contributors
"""Linux GUI back end (diskos_installer.owner) and the app's artwork: keys, signing, finding the Disc, placing the
update folder. No Tk display needed."""
import os
import shutil
import subprocess
import tempfile
import unittest

from diskos_installer import app, owner


def _write(p, data=b"x"):
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "wb") as f:
        f.write(data)


class KeysTests(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.d, ignore_errors=True)

    def test_status(self):
        self.assertEqual(owner.keys_status(None)[:2], (False, False))
        _write(os.path.join(self.d, "diskos-root.pub.pem"))
        self.assertEqual(owner.keys_status(self.d)[:2], (True, False))
        for f in owner.SIGN_FILES:
            _write(os.path.join(self.d, f))
        self.assertEqual(owner.keys_status(self.d), (True, True, []))

    def test_make_keys_refuses_existing(self):
        _write(os.path.join(self.d, "leaf.key"))
        with self.assertRaises(RuntimeError):
            owner.make_keys(self.d, lambda l: None)

    @unittest.skipUnless(shutil.which("openssl"), "needs openssl")
    def test_make_keys_then_sign(self):
        """Real keys and a real signing run (the release mq_ui), checked by the package's own six files."""
        keys = os.path.join(self.d, "keys")
        owner.make_keys(keys, lambda l: None)
        self.assertEqual(owner.keys_status(keys), (True, True, []))
        self.assertEqual(oct(os.stat(keys).st_mode & 0o777), "0o700")
        real = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "payload", "mq_ui")
        if not os.path.isfile(real):
            self.skipTest("needs payload/mq_ui (diskos-sign.sh only signs a real MIPS mq_ui)")
        ui = os.path.join(self.d, "ui", "mq_ui")
        os.makedirs(os.path.dirname(ui)); shutil.copyfile(real, ui)
        out = owner.sign_update(ui, keys, os.path.join(self.d, "out", "diskos-update"), lambda l: None)
        for f in owner.UPDATE_FILES:
            self.assertTrue(os.path.isfile(os.path.join(out, f)), f)

    def test_label(self):
        ui = os.path.join(self.d, "mq_ui")
        _write(ui)
        self.assertEqual(owner.update_label(ui), "custom")
        _write(os.path.join(self.d, "mq_ui.build.json"), b'{"version": "3.4.0-b1"}')
        self.assertEqual(owner.update_label(ui), "3.4.0-b1")
        _write(os.path.join(self.d, "mq_ui.build.json"), b'{"version": "bad label; rm"}')
        self.assertEqual(owner.update_label(ui), "custom")


class CardTests(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.sys = os.path.join(self.d, "sys")

    def tearDown(self):
        shutil.rmtree(self.d, ignore_errors=True)

    def _usb_disk(self, dev, vid, pid, serial, part):
        """sysfs: /sys/class/block/<part> -> .../usbX/<vid:pid device>/.../block/<dev>/<part>"""
        usb = os.path.join(self.sys, "devices", "usb1", "1-1")
        for f, v in (("idVendor", vid), ("idProduct", pid), ("serial", serial)):
            _write(os.path.join(usb, f), (v + "\n").encode())
        blk = os.path.join(usb, "1-1:1.0", "host0", "block", dev, part)
        os.makedirs(blk)
        os.makedirs(os.path.join(self.sys, "class", "block"), exist_ok=True)
        os.symlink(blk, os.path.join(self.sys, "class", "block", part))

    def test_disc_found_and_first(self):
        disc_mnt = os.path.join(self.d, "run", "media", "me", "SNOWSKY DISC")
        other = os.path.join(self.d, "run", "media", "me", "CARD")
        os.makedirs(disc_mnt); os.makedirs(other)
        self._usb_disk("sdb", "0525", "a4a5", "ingenic-storage", "sdb1")
        mounts = os.path.join(self.d, "mounts")
        with open(mounts, "w") as f:
            f.write(f"/dev/nvme0n1p2 / btrfs rw 0 0\n/dev/sdc1 {other} vfat rw 0 0\n"
                    f"/dev/sdb1 {disc_mnt.replace(' ', chr(92) + '040')} vfat rw 0 0\n")
        cards = owner.find_cards(mounts, self.sys)
        self.assertEqual(len(cards), 1)                  # sdc has no USB parent here: not offered
        self.assertTrue(cards[0]["disc"])
        self.assertEqual(cards[0]["path"], disc_mnt)

    def test_place_update_overwrites_each_file(self):
        """The six files are copied one by one over the previous run's (never the folder as a whole)."""
        src = os.path.join(self.d, "signed")
        for f in owner.UPDATE_FILES:
            _write(os.path.join(src, f), ("new " + f).encode())
        card = os.path.join(self.d, "card")
        for f in owner.UPDATE_FILES:
            _write(os.path.join(card, "diskos-update", f), b"old run")
        dest = owner.place_update(src, card, lambda l: None)
        for f in owner.UPDATE_FILES:
            with open(os.path.join(dest, f), "rb") as fh:
                self.assertEqual(fh.read(), ("new " + f).encode())
        self.assertFalse(os.path.exists(os.path.join(dest, "diskos-update")))   # never nested

    def test_place_update_creates_folder(self):
        src = os.path.join(self.d, "signed")
        for f in owner.UPDATE_FILES:
            _write(os.path.join(src, f), f.encode())
        dest = owner.place_update(src, os.path.join(self.d, "emptycard"), lambda l: None)
        self.assertEqual(sorted(os.listdir(dest)), sorted(owner.UPDATE_FILES))


class ArtworkTests(unittest.TestCase):
    def test_pngs_are_valid(self):
        for data in (app.pill_png(60, 20, "primary"), app.pill_png(60, 20, "ghost"), app.disc_png(24),
                     app.rule_png(30)):
            self.assertTrue(data.startswith(b"\x89PNG\r\n\x1a\n"))

    def test_iridescent_ramp_wraps(self):
        self.assertEqual([round(v) for v in app.iri_at(0)], [round(v) for v in app.iri_at(1.0)])


if __name__ == "__main__":
    unittest.main()
