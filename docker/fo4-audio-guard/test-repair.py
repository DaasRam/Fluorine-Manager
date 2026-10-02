#!/usr/bin/env python3
"""Exercise byte preservation, malformed input and repair rollback."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zlib

spec = importlib.util.spec_from_file_location("repair", Path(__file__).with_name("repair-ba2-audio.py"))
repair = importlib.util.module_from_spec(spec)
spec.loader.exec_module(repair)


def fixture(path, version=1, corrupt=False):
    files = [(b"Sound\\FX\\test.WAV", b"RIFF WAVE data" * 100, True),
             (b"music/test.xwm", b"RIFF XWMA data" * 100, True),
             (b"sound/stored.wav", b"unchanged stored audio", False),
             (b"sound/voice/test.fuz", b"compressed FUZ" * 100, True),
             (b"meshes/test.nif", b"compressed mesh" * 100, True)]
    records = []
    with path.open("wb") as stream:
        stream.seek(repair.HEADER.size + len(files) * repair.RECORD.size)
        for index, (name, plain, compressed) in enumerate(files):
            data = zlib.compress(plain) if compressed else plain
            if corrupt and index == 0:
                data = b"BAD" + data[3:]
            records.append((index, name.rsplit(b".", 1)[1].lower().ljust(4, b"\0"),
                            index + 10, 16, stream.tell(), len(data) if compressed else 0,
                            len(plain), 0xBAADF00D))
            stream.write(data)
        names_offset = stream.tell()
        for name, _, _ in files:
            stream.write(struct.pack("<H", len(name)) + name)
        stream.seek(0)
        stream.write(repair.HEADER.pack(b"BTDX", version, b"GNRL", len(files), names_offset))
        for record in records:
            stream.write(repair.RECORD.pack(*record))


class RepairTests(unittest.TestCase):
    def test_mixed_archives_preserve_bytes_and_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for version in (1, 7, 8):
                with self.subTest(version=version):
                    original, staged = root / "original.ba2", root / "staged.ba2"
                    fixture(original, version)
                    before = original.read_bytes()
                    repair.rewrite(original, staged)
                    self.assertEqual(len(repair.verify(original, staged)), 2)
                    self.assertEqual(repair.audit(staged)["audio_entries"], 0)
                    self.assertEqual(original.read_bytes(), before)
                    records = repair.directory(staged)[1]
                    self.assertEqual([bool(r[5]) for r in records], [False, False, False, True, True])

    def test_bad_entry_bounds_and_zlib_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            original, staged = Path(directory) / "bad.ba2", Path(directory) / "staged.ba2"
            fixture(original, corrupt=True)
            with self.assertRaises((ValueError, zlib.error)):
                repair.rewrite(original, staged)
            fixture(original)
            data = bytearray(original.read_bytes())
            struct.pack_into("<Q", data, repair.HEADER.size + 16, 0)
            original.write_bytes(data)
            with self.assertRaises(ValueError):
                repair.directory(original)

    def test_verification_detects_modified_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            original, staged = Path(directory) / "original.ba2", Path(directory) / "staged.ba2"
            fixture(original)
            repair.rewrite(original, staged)
            offset = repair.directory(staged)[1][0][4]
            with staged.open("r+b") as stream:
                stream.seek(offset)
                stream.write(b"BAD!")
            with self.assertRaises(ValueError):
                repair.verify(original, staged)

    @patch.object(repair, "require_game_closed")
    def test_backup_and_idempotence(self, _):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = root / "original.ba2"
            fixture(original)
            before = original.read_bytes()
            receipt = repair.repair([original], root / "backup")
            self.assertEqual(receipt["status"], "complete")
            info = receipt["archives"][0]
            self.assertEqual(Path(info["backup"]).read_bytes(), before)
            self.assertEqual(repair.sha256(original), info["sha256_after"])
            self.assertEqual(repair.repair([original], root / "again")["status"], "no_changes")

    @patch.object(repair, "require_game_closed")
    def test_partial_install_rolls_back(self, _):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first, second = root / "first.ba2", root / "second.ba2"
            fixture(first)
            fixture(second)
            before = first.read_bytes()
            real_replace = repair.os.replace

            def fail_second(source, destination):
                if destination == second:
                    raise OSError("injected install failure")
                return real_replace(source, destination)

            with patch.object(repair.os, "replace", side_effect=fail_second):
                with self.assertRaises(OSError):
                    repair.repair([first, second], root / "backup")
            self.assertEqual(first.read_bytes(), before)
            self.assertEqual(second.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
