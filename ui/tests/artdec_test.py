#!/usr/bin/env python3
"""Native decoder contracts for both sharp-cover clients; no device access."""
import pathlib
import struct
import subprocess
import tempfile
import unittest
import zlib


def png(width, height, rgb):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress((b"\0" + bytes(rgb) * width) * height)) +
            chunk(b"IEND", b""))


class SharpDecoderTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="diskos-artdec-test-")
        cls.root = pathlib.Path(cls.temp.name)
        cls.exe = cls.root / "artdec"
        source = pathlib.Path(__file__).resolve().parents[1] / "tools/diskos_artdec.c"
        subprocess.run(["cc", "-O2", "-Wall", "-Wextra", "-Werror", str(source),
                        "-o", str(cls.exe), "-lm"], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_sharp_dimensions_and_channel_order(self):
        source = self.root / "solid.png"
        source.write_bytes(png(32, 32, (209, 57, 12)))
        for mode, size in (("saver", 360), ("poster", 364)):
            with self.subTest(mode=mode):
                output = self.root / (mode + ".bgra")
                subprocess.run([str(self.exe), mode, str(source), str(output)], check=True)
                self.assertEqual(output.read_bytes(), bytes((12, 57, 209, 255)) * size * size)

    def test_malformed_picture_never_publishes(self):
        source = self.root / "broken.png"
        source.write_bytes(b"\x89PNG\r\n\x1a\n" + b"broken")
        output = self.root / "broken.bgra"
        result = subprocess.run([str(self.exe), "poster", str(source), str(output)])
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(output.exists())

    def test_existing_output_is_not_overwritten(self):
        source = self.root / "solid.png"
        source.write_bytes(png(32, 32, (209, 57, 12)))
        output = self.root / "existing.bgra"
        output.write_bytes(b"previous")
        result = subprocess.run([str(self.exe), "poster", str(source), str(output)])
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(output.read_bytes(), b"previous")


if __name__ == "__main__":
    unittest.main()
