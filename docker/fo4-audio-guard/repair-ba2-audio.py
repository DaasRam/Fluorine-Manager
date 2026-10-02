#!/usr/bin/env python3
"""Store compressed WAV/XWM entries in explicitly selected Fallout 4 BA2s.

Dry-run by default. --apply requires a new backup directory. Originals are
backed up before replacement, and every entry's bytes are verified. FUZ voice
containers support archive compression and are deliberately left alone.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib

HEADER = struct.Struct("<4sI4sIQ")
RECORD = struct.Struct("<I4sIIQIII")
STREAMED_AUDIO = {b"wav", b"xwm", b"xma", b"ogg", b"mp3"}


def read_exact(stream, size):
    data = stream.read(size)
    if len(data) != size:
        raise ValueError("truncated archive")
    return data


def directory(path):
    with path.open("rb") as stream:
        header = HEADER.unpack(read_exact(stream, HEADER.size))
        magic, version, kind, count, names_offset = header
        length = os.fstat(stream.fileno()).st_size
        table_end = HEADER.size + count * RECORD.size
        if magic != b"BTDX" or kind != b"GNRL" or version not in (1, 7, 8):
            raise ValueError(f"unsupported Fallout 4 general BA2: {path}")
        if count > 1000000 or not table_end <= names_offset <= length:
            raise ValueError("invalid archive directory bounds")
        records = [list(RECORD.unpack(read_exact(stream, RECORD.size))) for _ in range(count)]
        for record in records:
            offset, packed, unpacked = record[4:7]
            if not table_end <= offset <= offset + (packed or unpacked) <= names_offset:
                raise ValueError("entry payload outside archive data region")
        stream.seek(names_offset)
        names = [read_exact(stream, int.from_bytes(read_exact(stream, 2), "little"))
                 for _ in range(count)]
        stream.seek(names_offset)
        names_blob = stream.read()
    return header, records, names, names_blob


def selected(record, name):
    extension = record[1].rstrip(b"\0").lower()
    if record[5] and extension in STREAMED_AUDIO:
        if name.rsplit(b".", 1)[-1].lower() != extension:
            raise ValueError("entry extension/name mismatch")
        return True
    return False


def payload(stream, record):
    stream.seek(record[4])
    return read_exact(stream, record[5] or record[6])


def decode(data, size):
    decoder = zlib.decompressobj()
    plain = decoder.decompress(data, size + 1)
    if len(plain) != size or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise ValueError("invalid compressed audio payload or size")
    return plain


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def fingerprint(path):
    stat = path.stat()
    return stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns


def audit(path):
    _, records, names, _ = directory(path)
    targets = [(record, name) for record, name in zip(records, names) if selected(record, name)]
    return {"path": str(path), "entries": len(records), "audio_entries": len(targets),
            "counts": dict(Counter(r[1].rstrip(b"\0").decode("ascii").lower() for r, _ in targets)),
            "extra_bytes": sum(r[6] - r[5] for r, _ in targets)}


def rewrite(original, staged):
    header, records, names, names_blob = directory(original)
    rewritten = []
    with original.open("rb") as source, staged.open("wb") as dest:
        dest.seek(HEADER.size + len(records) * RECORD.size)
        for old, name in zip(records, names):
            record = old.copy()
            data = payload(source, old)
            if selected(old, name):
                data = decode(data, old[6])
                record[5] = 0
            record[4] = dest.tell()
            dest.write(data)
            rewritten.append(record)
        names_offset = dest.tell()
        dest.write(names_blob)
        dest.seek(0)
        dest.write(HEADER.pack(*header[:4], names_offset))
        for record in rewritten:
            dest.write(RECORD.pack(*record))
        dest.flush()
        os.fsync(dest.fileno())
    shutil.copystat(original, staged)


def verify(original, staged):
    old_header, old_records, old_names, old_blob = directory(original)
    new_header, new_records, new_names, new_blob = directory(staged)
    if old_header[:4] != new_header[:4] or old_names != new_names or old_blob != new_blob:
        raise ValueError("archive identity/name table changed")
    changed = []
    with original.open("rb") as source, staged.open("rb") as dest:
        for old, new, name in zip(old_records, new_records, old_names):
            if old[:4] != new[:4] or old[6:] != new[6:]:
                raise ValueError("entry identity/size/flags changed")
            expected = payload(source, old)
            if selected(old, name):
                expected = decode(expected, old[6])
                if new[5] != 0:
                    raise ValueError("audio entry still compressed")
                changed.append({"name": name.decode("utf-8", errors="backslashreplace"),
                                "sha256_decoded": hashlib.sha256(expected).hexdigest()})
            elif old[5] != new[5]:
                raise ValueError("non-target compression changed")
            if expected != payload(dest, new):
                raise ValueError(f"entry bytes changed: {name!r}")
    return changed


def require_game_closed():
    for proc in Path("/proc").iterdir():
        if not proc.name.isdigit():
            continue
        try:
            command = (proc / "comm").read_text().strip().lower()
        except (OSError, UnicodeError):
            continue
        if command in {"fallout4.exe", "f4se_loader.exe"}:
            raise RuntimeError(f"close Fallout 4 before repairing archives (PID {proc.name})")


def sync_directory(path):
    descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def save_receipt(backup_dir, receipt):
    temp = backup_dir / "receipt.json.tmp"
    with temp.open("w") as stream:
        json.dump(receipt, stream, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temp, backup_dir / "receipt.json")
    sync_directory(backup_dir)


def repair(paths, backup_dir):
    require_game_closed()
    # Inspect the complete batch before creating backups or staging files.
    paths = list(dict.fromkeys(path.resolve(strict=True) for path in paths))
    targets = [info for path in paths if (info := audit(path))["audio_entries"]]
    if not targets:
        return {"status": "no_changes", "archives": []}
    backup_dir.mkdir(parents=True, exist_ok=False)
    receipt = {"status": "staging", "archives": targets}
    save_receipt(backup_dir, receipt)
    installed = []
    staged_paths = []
    try:
        for info in targets:
            original = Path(info["path"])
            state = fingerprint(original)
            backup = backup_dir / original.relative_to(original.anchor)
            backup.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(["cp", "--reflink=auto", "--preserve=mode,timestamps", "--",
                            str(original), str(backup)], check=True)
            with backup.open("rb") as stream:
                os.fsync(stream.fileno())
            before_hash = sha256(original)
            if before_hash != sha256(backup) or fingerprint(original) != state:
                raise ValueError(f"archive changed during backup: {original}")
            descriptor, filename = tempfile.mkstemp(prefix=".FluorineAudioRepair-", dir=original.parent)
            os.close(descriptor)
            staged = Path(filename)
            staged_paths.append(staged)
            rewrite(backup, staged)
            changes = verify(backup, staged)
            info.update(backup=str(backup), staged=str(staged), original_fingerprint=state,
                        sha256_before=before_hash, sha256_after=sha256(staged), changed=changes,
                        status="verified")
            save_receipt(backup_dir, receipt)
            print(f"Verified {len(changes)} repaired audio entries: {original}", flush=True)
        require_game_closed()
        for info in targets:
            if fingerprint(Path(info["path"])) != info["original_fingerprint"]:
                raise ValueError(f"archive changed while staging: {info['path']}")
        receipt["status"] = "installing"
        save_receipt(backup_dir, receipt)
        for info in targets:
            original = Path(info["path"])
            os.replace(info["staged"], original)
            installed.append(info)
            sync_directory(original.parent)
            info["status"] = "installed"
            save_receipt(backup_dir, receipt)
        receipt["status"] = "complete"
        save_receipt(backup_dir, receipt)
        return receipt
    except BaseException:
        # Backups remain available, including after a failed or partial repair.
        for info in reversed(installed):
            restored = Path(info["staged"])
            subprocess.run(["cp", "--reflink=auto", "--preserve=mode,timestamps", "--",
                            info["backup"], str(restored)], check=True)
            os.replace(restored, info["path"])
            sync_directory(Path(info["path"]).parent)
            info["status"] = "restored"
        receipt["status"] = "failed"
        save_receipt(backup_dir, receipt)
        raise
    finally:
        for staged in staged_paths:
            staged.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archives", type=Path, nargs="*")
    parser.add_argument("--plan", type=Path, help="JSON array of objects containing archive path fields")
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--backup-dir", type=Path)
    args = parser.parse_args()
    paths = args.archives
    if args.plan:
        paths += [Path(item["path"]) for item in json.loads(args.plan.read_text())]
    if not paths or (args.apply and not args.backup_dir):
        parser.error("provide archives; --apply also requires --backup-dir")
    if args.apply:
        result = repair(paths, args.backup_dir)
        print(f"{result['status']}: backups and verification receipt in {args.backup_dir}")
    else:
        print(json.dumps([audit(path) for path in paths], indent=2))


if __name__ == "__main__":
    main()
