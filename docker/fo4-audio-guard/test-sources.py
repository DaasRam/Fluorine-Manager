#!/usr/bin/env python3
"""Check resource-name attribution, including a vanilla legacy-encoded name."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("sources", Path(__file__).with_name("index-sources.py"))
sources = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sources)


class SourceIndex(unittest.TestCase):
    def test_vanilla_hash(self):
        name = r"sound\fx\npc\dogmeat\bark\standard\npc_dogmeat_barkstandard_layerc_13.xwm"
        self.assertEqual(sources.resource_id(name), (0x0005DFA0, 0x006D7778, 0xAEB0CBA2))

    def test_archive_names_and_loose_candidates(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            loose = root / "Sound/test.wav"
            loose.parent.mkdir()
            loose.write_bytes(b"")
            names = [b"Sound/test.wav", b"Sound/Voice/Mar\xeda_F.fuz"]
            keys = [sources.resource_id("Sound/test.wav"), (123, 0x7A7566, 456)]
            records = b"".join(struct.pack("<I4sIIQIII", key[0], key[1].to_bytes(4, "little"),
                                         key[2], 0, 0, 0, 0, 0) for key in keys)
            archive = root / "test.ba2"
            archive.write_bytes(struct.pack("<4sI4sIQ", b"BTDX", 1, b"GNRL", 2, 24 + len(records)) +
                                records + b"".join(struct.pack("<H", len(name)) + name for name in names))
            entries = list(sources.archive_entries(archive))
            self.assertEqual(entries, [(keys[0], "Sound/test.wav"), (keys[1], "Sound/Voice/María_F.fuz")])
            output = root / "sources.tsv"
            count, failures = sources.index([root], output)
            self.assertEqual((count, failures), (3, []))
            text = output.read_text()
            self.assertIn("[loose:", text)
            self.assertIn("[archive:", text)
            self.assertIn(f"{keys[0][0]:08x}:00000000:{keys[0][2]:08x}", text)
            archive.write_bytes(archive.read_bytes()[:-3])
            with self.assertRaisesRegex(ValueError, "truncated BA2 name"):
                list(sources.archive_entries(archive))


if __name__ == "__main__":
    unittest.main()
