#!/usr/bin/env python3
"""Offline protocol fixture used only by run_workspace_smoke.py --installer."""
import json
import os
from pathlib import Path
import sys

base = Path(os.environ['FLUORINE_UI_SMOKE_DIR'])
assert (base / 'isolated-test-fixture').is_file()


def emit(kind, **values):
    print(json.dumps({'type': kind, **values}), flush=True)


if sys.argv[1] == 'gallery':
    print(json.dumps({'modlists': [{
        'title': 'Desert Workshop', 'author': 'Fixture author', 'game': 'falloutnewvegas',
        'machine_name': 'fluorine-offline-fixture', 'version': '2.4.1',
        'description': 'Synthetic modlist for native installer verification.',
        'date_updated': '2026-09-27T10:31:00.8081925Z',
        'links': {'download': str(base / 'fixture.wabbajack'),
                  'readme': 'https://example.invalid/instructions'},
        'download_metadata': {'SizeOfArchives': 1000000, 'SizeOfInstalledFiles': 2000000,
                              'NumberOfArchives': 36}
    }]}))
    sys.exit(0)

assert sys.argv[1] == 'install'
downloads, installed = Path(sys.argv[3]), Path(sys.argv[4])
assert downloads.is_relative_to(base) and installed.is_relative_to(base)
emit('hello', protocol_version=1, engine_version='offline-fixture')
assert json.loads(sys.stdin.readline())['type'] == 'hello_ack'
emit('plan_ready', name='Desert Workshop', author='Fixture author', version='2.4.1',
     archive_count=36, artifacts=[])
attempt = base / 'engine-attempts.txt'
number = int(attempt.read_text()) + 1 if attempt.exists() else 1
attempt.write_text(str(number))
emit('PhaseChange', phase='Downloading')
emit('DownloadSkipped', count=6, total_size=18000000)
(downloads / 'retained-archive.bin').write_bytes(b'preserve this cached download')

if number == 1:
    for i in range(30):
        emit('item_started', item_id=f'item-{i}', name=f'archive-{i}.7z',
             display_name=f'Environment pack {i + 1}', subtitle=f'archive-{i}.7z',
             stage='Downloading', total=100000000, unit='bytes')
        emit('item_progress', item_id=f'item-{i}', completed=(i + 1) * 1500000,
             total=100000000, speed=2300000, unit='bytes')
        if i >= 25:
            emit('item_completed', item_id=f'item-{i}')
    emit('item_failed', item_id='item-24', message='Fixture connection interrupted; retry after resume.')
    emit('ArchiveComplete', index=11, total=36)
    for i in range(2):
        emit('manual_download_required', request_id=f'manual-{i}', archive_name=f'Manual pack {i + 1}.7z',
             url='https://example.invalid/archive', prompt='Select a previously downloaded archive.',
             expected_size=12345, expected_hash='fixture')
    for line in sys.stdin:
        if json.loads(line)['type'] == 'cancel':
            emit('cancelled')
            sys.exit(0)
else:
    assert (downloads / 'retained-archive.bin').read_bytes() == b'preserve this cached download'
    emit('PhaseChange', phase='Building files')
    emit('item_started', item_id='build-1', name='Build modlist files', stage='Building',
         total=500, unit='directives')
    emit('DirectiveComplete', index=500, total=500)
    emit('item_completed', item_id='build-1')
    # The unknown fixture adapter performs no patching. A valid isolated setup
    # lets the real post-install stage register this temporary Library entry.
    (installed / 'ModOrganizer.ini').write_text(
        (base / 'Desert Workshop' / 'ModOrganizer.ini').read_text())
    emit('install_completed', stats={'archives_downloaded': 30, 'archives_skipped': 6,
                                     'archives_failed': 0})
