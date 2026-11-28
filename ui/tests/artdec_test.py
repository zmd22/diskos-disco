#!/usr/bin/env python3
"""Native decoder contracts for both sharp-cover clients; no device access."""
import base64
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


def ogg_page(packet, serial, sequence, flags=0, continues=False):
    lacing = []
    remaining = len(packet)
    while remaining >= 255:
        lacing.append(255)
        remaining -= 255
    if remaining or not continues:
        lacing.append(remaining)
    header = (b"OggS\0" + bytes((flags,)) + b"\0" * 8 +
              struct.pack("<II", serial, sequence) + b"\0" * 4 +
              bytes((len(lacing),)) + bytes(lacing))
    page = bytearray(header + packet)
    crc = 0
    for byte in page:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if crc & 0x80000000 else (crc << 1) & 0xFFFFFFFF
    page[22:26] = struct.pack("<I", crc)
    return bytes(page)


def flac_picture(image):
    return (struct.pack(">I", 3) + struct.pack(">I", 9) + b"image/png" +
            struct.pack(">I", 0) + struct.pack(">IIIII", 4, 4, 24, 0, len(image)) + image)


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
        for mode, size in (("saver", 360), ("poster", 364), ("roulette", 224)):
            with self.subTest(mode=mode):
                output = self.root / (mode + ".bgra")
                subprocess.run([str(self.exe), mode, str(source), str(output)], check=True)
                self.assertEqual(output.read_bytes(), bytes((12, 57, 209, 255)) * size * size)

    def test_roulette_budget_rejects_large_decode_and_invalid_caps(self):
        source = self.root / "budget.png"
        source.write_bytes(png(1200,1200,(10,20,30)))
        output = self.root / "budget.bgra"
        self.assertEqual(subprocess.run([str(self.exe),"roulette",str(source),str(output),"4096"]).returncode,6)
        self.assertFalse(output.exists())
        self.assertEqual(subprocess.run([str(self.exe),"roulette",str(source),str(output),"16384"]).returncode,0)
        self.assertEqual(output.stat().st_size,224*224*4)
        for cap in ("0","32768","oops","-1"):
            self.assertEqual(subprocess.run([str(self.exe),"roulette",str(source),str(output),cap]).returncode,2)

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

    def test_opus_ogg_cover_art_across_pages(self):
        image = png(4, 4, (209, 57, 12))
        picture = base64.b64encode(flac_picture(image))
        comment = b"METADATA_BLOCK_PICTURE=" + picture
        tags = (b"OpusTags" + struct.pack("<I", 200) + b"test" + b"x" * 196 +
                struct.pack("<I", 1) + struct.pack("<I", len(comment)) + comment)
        source = self.root / "cover.opus"
        head = b"OpusHead" + b"\x01\x02" + b"\0" * 9
        source.write_bytes(
            ogg_page(head, 1234, 0, 2) +
            ogg_page(tags[:255], 1234, 1, 0, continues=True) +
            ogg_page(tags[255:], 1234, 2, 1))

        output = self.root / "opus.bgra"
        subprocess.run([str(self.exe), "poster", str(source), str(output)], check=True)
        self.assertEqual(output.read_bytes(), bytes((12, 57, 209, 255)) * 364 * 364)

    def opus_file(self, name, value, prefix=b""):
        comment = b"METADATA_BLOCK_PICTURE=" + value
        tags = (b"OpusTags" + struct.pack("<I", 4) + b"test" +
                struct.pack("<I", 1) + struct.pack("<I", len(comment)) + comment)
        head = b"OpusHead" + b"\x01\x02" + b"\0" * 9
        source = self.root / name
        source.write_bytes(prefix + ogg_page(head, 1234, 0, 2) + ogg_page(tags, 1234, 1))
        return source

    def test_opus_cover_base64_variants(self):
        # a one-colour picture whose FLAC picture block is not a multiple of 3 bytes, so its base64 carries padding
        image = next(png(w, 4, (209, 57, 12)) for w in range(4, 12) if len(flac_picture(png(w, 4, (209, 57, 12)))) % 3)
        encoded = base64.b64encode(flac_picture(image))
        self.assertTrue(encoded.endswith(b"="))   # the unpadded case below really drops padding
        wrapped = b"\n".join(encoded[i:i + 76] for i in range(0, len(encoded), 76))
        for label, value in (("unpadded", encoded.rstrip(b"=")), ("wrapped", wrapped),
                             ("crlf", wrapped.replace(b"\n", b"\r\n") + b"\r\n")):
            with self.subTest(label=label):
                source = self.opus_file(label + ".opus", value)
                output = self.root / (label + ".bgra")
                subprocess.run([str(self.exe), "poster", str(source), str(output)], check=True)
                self.assertEqual(output.read_bytes(), bytes((12, 57, 209, 255)) * 364 * 364)
        for label, value in (("short-pad", encoded.rstrip(b"=") + b"=" * 3),
                             ("data-after-pad", encoded + b"QQ=="), ("bad-char", encoded[:8] + b"*" + encoded[9:])):
            with self.subTest(label=label):
                source = self.opus_file(label + ".opus", value)
                output = self.root / (label + ".bgra")
                result = subprocess.run([str(self.exe), "poster", str(source), str(output)], capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())

    def test_id3_tag_before_ogg(self):
        image = png(4, 4, (209, 57, 12))
        frame = b"TIT2" + struct.pack(">I", 3) + b"\0\0" + b"\0ab"
        size = bytes(((len(frame) >> 21) & 127, (len(frame) >> 14) & 127, (len(frame) >> 7) & 127, len(frame) & 127))
        source = self.opus_file("id3.opus", base64.b64encode(flac_picture(image)),
                                prefix=b"ID3\x04\0\0" + size + frame)
        output = self.root / "id3.bgra"
        subprocess.run([str(self.exe), "poster", str(source), str(output)], check=True)
        self.assertEqual(output.read_bytes(), bytes((12, 57, 209, 255)) * 364 * 364)

    def test_vorbis_ogg_cover_art(self):
        image = png(4, 4, (209, 57, 12))
        ident = (b"\x01vorbis" + struct.pack("<I", 0) + b"\x02" +
                 struct.pack("<I", 48000) + struct.pack("<iii", 0, 128000, 0) + b"\xb6\x01")
        self.assertEqual(len(ident), 30)
        comments = (
            ("METADATA_BLOCK_PICTURE", base64.b64encode(flac_picture(image))),
            ("COVERART", base64.b64encode(image)),
        )
        for key, value in comments:
            with self.subTest(key=key):
                comment = key.encode() + b"=" + value
                packet = (b"\x03vorbis" + struct.pack("<I", 4) + b"test" +
                          struct.pack("<I", 1) + struct.pack("<I", len(comment)) + comment + b"\x01")
                source = self.root / (key + ".ogg")
                source.write_bytes(ogg_page(ident, 5678, 0, 2) + ogg_page(packet, 5678, 1))

                output = self.root / (key + ".bgra")
                subprocess.run([str(self.exe), "poster", str(source), str(output)], check=True)
                self.assertEqual(output.read_bytes(), bytes((12, 57, 209, 255)) * 364 * 364)


if __name__ == "__main__":
    unittest.main()
