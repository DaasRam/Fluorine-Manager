#!/usr/bin/env python3
"""Index active loose audio and BA2 names for FluorineAudioGuard failure logs.

This reads archive directories only, never extracts or changes audio. The log
lists matching candidates: archive load order and runtime file selection are
not inferred from a resource hash. Rebuild after changing the mod list.
"""
import argparse
import os
from pathlib import Path
import struct
import sys
import zlib

AUDIO = {".wav", ".xwm", ".fuz", ".xma"}


def crc(text):
    # Bethesda's CRC32 starts at zero and omits the final complement.
    return zlib.crc32(text.encode("utf-8"), 0xFFFFFFFF) ^ 0xFFFFFFFF


def resource_id(name):
    path = name.replace("/", "\\").lower()
    directory, _, filename = path.rpartition("\\")
    stem, _, extension = filename.rpartition(".")
    return crc(stem), int.from_bytes(extension.encode("ascii")[:4].ljust(4, b"\0"), "little"), crc(directory)


def archive_entries(path):
    with path.open("rb") as stream:
        header = stream.read(24)
        if len(header) != 24:
            raise ValueError("truncated BA2 header")
        magic, version, kind, count, names = struct.unpack("<4sI4sIQ", header)
        if magic != b"BTDX":
            raise ValueError("not a BA2 archive")
        if kind != b"GNRL":
            return
        if version not in (1, 7, 8):
            raise ValueError(f"unsupported general BA2 version {version}")
        length = os.fstat(stream.fileno()).st_size
        if count > 1000000 or 24 + count * 36 > length or not (24 <= names <= length):
            raise ValueError("invalid BA2 directory bounds")
        records = stream.read(count * 36)
        stream.seek(names)
        for i in range(count):
            size = stream.read(2)
            if len(size) != 2:
                raise ValueError("truncated BA2 name table")
            size = int.from_bytes(size, "little")
            raw = stream.read(size)
            if len(raw) != size:
                raise ValueError("truncated BA2 name")
            try:
                name = raw.decode("utf-8")
            except UnicodeDecodeError:
                name = raw.decode("cp1252")
            if Path(name.replace("\\", "/")).suffix.lower() not in AUDIO:
                continue
            file_hash, extension, directory_hash = struct.unpack_from("<I4sI", records, i * 36)
            key = file_hash, int.from_bytes(extension, "little"), directory_hash
            # A layout/hash mismatch must not silently generate misleading names.
            # Non-ASCII archive names can use a legacy Windows code page and
            # locale-specific folding. Their recorded IDs remain authoritative.
            if name.isascii() and key != resource_id(name):
                raise ValueError(f"BA2 hash/name mismatch for {name}")
            yield key, name


def index(roots, output):
    entries = set()
    failures = []
    for root in roots:
        if not root.is_dir():
            failures.append(f"missing data directory: {root}")
            continue
        for directory, subdirs, files in os.walk(root):
            subdirs[:] = [d for d in subdirs if d not in (".git", ".mohidden")]
            for filename in files:
                path = Path(directory) / filename
                extension = path.suffix.lower()
                if extension in AUDIO:
                    name = str(path.relative_to(root)).replace("/", "\\")
                    entries.add((resource_id(name), f"{name} [loose: {path}]"))
                elif extension == ".ba2":
                    try:
                        for key, name in archive_entries(path):
                            entries.add((key, f"{name} [archive: {path}]"))
                    except (OSError, ValueError, UnicodeError) as error:
                        failures.append(f"{path}: {error}")
    # Fallout's resource loader tries different audio extensions. Add an alias
    # without the extension for a failure logged before a format was selected.
    with output.open("w", encoding="utf-8", newline="\n") as stream:
        for key, label in sorted(entries):
            label = label.replace("\t", " ").replace("\r", " ").replace("\n", " ")
            for actual in (key, (key[0], 0, key[2])):
                stream.write(":".join(f"{value:08x}" for value in actual) + "\t" + label + "\n")
    return len(entries), failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-data", type=Path, required=True)
    parser.add_argument("--mods", type=Path, required=True)
    parser.add_argument("--modlist", type=Path, required=True)
    parser.add_argument("--overwrite", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    active = [line[1:] for line in args.modlist.read_text(encoding="utf-8-sig").splitlines()
              if line.startswith("+") and not line.endswith("_separator")]
    roots = [args.game_data] + [args.mods / name for name in reversed(active)]
    if args.overwrite and args.overwrite.is_dir():
        roots.append(args.overwrite)
    count, failures = index(roots, args.output)
    print(f"Indexed {count} audio asset candidates from {len(roots)} directories into {args.output}")
    for failure in failures:
        print(f"WARNING: {failure}", file=sys.stderr)
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
