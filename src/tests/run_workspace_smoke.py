#!/usr/bin/env python3
"""Exercise the packaged native UI with synthetic data in a temporary directory.

First run BUILD_JOBS=4 ./build.sh test and BUILD_JOBS=4 ./build.sh.
Screenshots and checks.json are retained in the printed temporary directory.
No game is launched. The production app does not contain the probe.
"""
import argparse
import os
import json
import pathlib
import struct
import subprocess
import tempfile
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--theme', default='dark.qss', help='Bundled stylesheet name; empty uses Qt light theme')
parser.add_argument('--registered-alias', action='store_true', help='Exercise a registered symlink to the active setup')
parser.add_argument('--fresh-layout', action='store_true', help='Check the default without a saved legacy layout')
parser.add_argument('--installer', action='store_true', help='Exercise the native modlist installer with an offline fixture engine')
parser.add_argument('--management', action='store_true', help='Exercise profile and executable dialogs in the isolated setup')
parser.add_argument('--font-size', type=int, default=0, help='App font override in pixels; zero uses the theme default')
args = parser.parse_args()
if args.installer and args.management:
    parser.error('--installer and --management are separate fixture modes')
if not 0 <= args.font_size <= 48:
    parser.error('--font-size must be between 0 and 48')
repo = pathlib.Path(__file__).resolve().parents[2]
bundle = repo/'build/fluorine-manager'
# Startup migration uses the real home directory, independently of XDG. Never
# run this fixture where it could move a user's legacy Flatpak installation.
legacy_data = pathlib.Path.home()/'.var/app/com.fluorine.manager'
if legacy_data.exists() or legacy_data.is_symlink():
    sys.exit('Native smoke requires a test account without legacy Flatpak data.')
base = pathlib.Path(tempfile.mkdtemp(prefix='fluorine-native-smoke-'))
(base/'isolated-test-fixture').write_text('Synthetic test data; never launch a game here.\n')
setup = base/'Desert Workshop'
game = base/'game'
for rel in ['Data', 'Data/Textures']:
    (game/rel).mkdir(parents=True, exist_ok=True)
(game/'FalloutNV.exe').write_bytes(b'MZ'+b'\0'*126)
(game/'Fallout_default.ini').write_text('[General]\nsLanguage=ENGLISH\n')
for rel in ['mods','downloads','overwrite','profiles/Everyday','profiles/Testing','cache']:
    (setup/rel).mkdir(parents=True, exist_ok=True)
names = ['Weather and Lighting','World Textures','Interface Improvements','Community Patch','Environment Detail','Audio Resources','Gameplay Tweaks','Distant Terrain','Animation Pack','Character Overhaul','Map Markers','Compass Improvements']
for i,name in enumerate(names):
    mod=setup/'mods'/name
    (mod/'textures').mkdir(parents=True)
    (mod/'textures'/('fixture-%d.dds'%i)).write_bytes(b'fixture')
    (mod/'meta.ini').write_text('[General]\ngameName=New Vegas\nversion=1.2.0\ncategory=1\nauthor=Fixture author\nnotes=Temporary UI test mod.\n')
hedr=struct.pack('<4sHfII',b'HEDR',12,1.34,0,2048)
record=struct.pack('<4sIIIIHH',b'TES4',len(hedr),1,0,0,15,0)+hedr
(game/'Data/FalloutNV.esm').write_bytes(record)
(game/'Data/DeadMoney.esm').write_bytes(record)
(setup/'mods/Weather and Lighting/Weather.esp').write_bytes(record)
for profile in ['Everyday','Testing']:
    p=setup/'profiles'/profile
    (p/'modlist.txt').write_text('# Synthetic UI fixture\n'+''.join('+'+n+'\n' for n in reversed(names)))
    (p/'plugins.txt').write_text('Weather.esp\n')
    (p/'loadorder.txt').write_text('FalloutNV.esm\nWeather.esp\n')
    (p/'initweaks.ini').write_text('')
    (p/'settings.ini').write_text('[General]\nlocal_inis=true\nlocal_saves=true\n')
    (p/'fallout.ini').write_text('[General]\nsLanguage=ENGLISH\n')
    (p/'falloutprefs.ini').write_text('[Display]\niSize W=1280\niSize H=720\n')
(setup/'categories.dat').write_text('1|Visuals|0|0\n')
(setup/'ModOrganizer.ini').write_text(f'''[General]
gameName=New Vegas
gamePath={game}
game_edition=GOG
selected_profile=Everyday
first_start=false
version=0.0.1
[Settings]
check_for_updates=false
autocheck_update_install=false
style={args.theme}
qss_font_size={args.font_size}
mod_directory={setup}/mods
download_directory={setup}/downloads
profiles_directory={setup}/profiles
cache_directory={setup}/cache
overwrite_directory={setup}/overwrite
[Fluorine]
{'' if args.fresh_layout else 'workspaceView=0'}
workspaceDestination=mods
workspaceFilters=false
[PluginPersistance]
Bethesda%20Plugin%20Manager\\enabled=true
''')
for kind in ['config','data','cache','runtime']:
    (base/kind).mkdir()
(base/'runtime').chmod(0o700)
# An existing empty file prevents legacy credential migration from the real
# home directory. The probe must remain offline and signed out.
credentials = base/'config/ModOrganizer/credentials.ini'
credentials.parent.mkdir(parents=True)
credentials.touch(mode=0o600)
registry = base/'config/Mod Organizer Team/Mod Organizer.conf'
if args.registered_alias:
    (base/'setup-alias').symlink_to(setup, target_is_directory=True)
    registry.parent.mkdir(parents=True, exist_ok=True)
    registry.write_text(f'[General]\nPortableInstances={base}/setup-alias\n')
registry_before = registry.read_bytes() if registry.exists() else b''
# Keep Qt's Documents location isolated too; do not change the user's HOME.
(base/'Documents').mkdir()
(base/'config/user-dirs.dirs').write_text(f'XDG_DOCUMENTS_DIR="{base}/Documents"\n')
(base/'fonts.conf').write_text(f'''<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig><dir>{bundle}/fonts</dir><dir>/usr/share/fonts</dir>
<dir>/usr/local/share/fonts</dir><cachedir prefix="xdg">fontconfig</cachedir>
<alias><family>sans-serif</family><prefer><family>DejaVu Sans</family></prefer></alias>
<alias><family>Segoe UI</family><prefer><family>DejaVu Sans</family></prefer></alias>
<alias><family>MS Shell Dlg 2</family><prefer><family>DejaVu Sans</family></prefer></alias>
</fontconfig>''')
env=os.environ.copy()
for k in ['PYTHONPATH','PYTHONHOME','MO2_PYTHON_DIR','FLUORINE_ALLOW_INCOMPATIBLE_PLUGINS']:
    env.pop(k,None)
env.update({
 'XDG_CONFIG_HOME':str(base/'config'),'XDG_DATA_HOME':str(base/'data'),
 'XDG_CACHE_HOME':str(base/'cache'),'XDG_RUNTIME_DIR':str(base/'runtime'),
 'MO2_BASE_DIR':str(bundle),'MO2_PLUGINS_DIR':str(bundle/'plugins'),'MO2_LIBS_DIR':str(bundle/'lib'),
 'LD_LIBRARY_PATH':str(bundle/'lib'),
 'LD_PRELOAD':str(repo/'build/src/tests/libworkspace_smoke_probe.so'),
 'QT_PLUGIN_PATH':str(bundle/'qt6plugins'),'QT_QPA_PLATFORM_PLUGIN_PATH':str(bundle/'qt6plugins/platforms'),
 'QT_QPA_PLATFORM':'offscreen','QT_QPA_PLATFORMTHEME':'','QTWEBENGINE_DISABLE_SANDBOX':'1',
 'QTWEBENGINEPROCESS_PATH':str(bundle/'libexec/QtWebEngineProcess'),
 'QTWEBENGINE_RESOURCES_PATH':str(bundle/'resources'),
 'QTWEBENGINE_LOCALES_PATH':str(bundle/'translations/qtwebengine_locales'),
 'QT_LOGGING_RULES':'default.debug=false','FLUORINE_UI_SMOKE_DIR':str(base),
 'FONTCONFIG_FILE':str(base/'fonts.conf'),'FONTCONFIG_PATH':str(base),
})
if args.installer:
    (base/'fixture.wabbajack').write_bytes(b'offline protocol fixture')
    engine = base/'fixture-engine'
    engine.write_bytes((repo/'src/tests/installer_smoke_engine.py').read_bytes())
    engine.chmod(0o700)
    env['FLUORINE_CLF3_PATH'] = str(engine)
    env['LD_PRELOAD'] = str(repo/'build/src/tests/libinstaller_smoke_probe.so')
if args.management:
    env['LD_PRELOAD'] = str(repo/'build/src/tests/libmanagement_smoke_probe.so')
print(base,flush=True)
with (base/'run.log').open('w') as log:
    r=subprocess.run([str(bundle/'ModOrganizer-core'),'--multiple','-i',str(setup)],cwd=bundle,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=65)
print('exit',r.returncode)
if args.management and r.returncode == 0 and (base/'management-expected.json').is_file():
    env['FLUORINE_UI_SMOKE_RESTORE_MANAGEMENT'] = '1'
    with (base/'restore.log').open('w') as log:
        restored = subprocess.run([str(bundle/'ModOrganizer-core'),'--multiple','-i',str(setup)],cwd=bundle,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=65)
    print('reopen exit',restored.returncode)
    result = json.loads((base/'checks.json').read_text())
    if (base/'checks-restore.json').is_file():
        result['checks'].extend(json.loads((base/'checks-restore.json').read_text())['checks'])
    else:
        result['checks'].append({'check': 'Verify profile and program settings after restarting', 'pass': False})
    (base/'checks.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))
    sys.exit(0 if restored.returncode == 0 and all(c['pass'] for c in result['checks']) else 1)
if args.installer or args.management:
    if (base/'checks.json').exists():
        print((base/'checks.json').read_text())
    else:
        print((base/'run.log').read_text()[-6000:])
    sys.exit(r.returncode if (base/'checks.json').exists() else 1)
restore_code = 1
if r.returncode == 0 and (base/'manual-columns.json').is_file():
    env['FLUORINE_UI_SMOKE_RESTORE_COLUMNS'] = '1'
    with (base/'restore.log').open('w') as log:
        restored = subprocess.run([str(bundle/'ModOrganizer-core'),'--multiple','-i',str(setup)],cwd=bundle,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=65)
    restore_code = restored.returncode
    print('reopen exit',restore_code)
if (base/'checks.json').exists():
    result = json.loads((base/'checks.json').read_text())
    if (base/'checks-restore.json').is_file():
        result['checks'].extend(json.loads((base/'checks-restore.json').read_text())['checks'])
    else:
        result['checks'].append({'check': 'Reopen application and verify saved column widths', 'pass': False})
    registry_after = registry.read_bytes() if registry.exists() else b''
    result['checks'].append({'check': 'Library browsing did not rewrite setup registration',
                             'pass': registry_before == registry_after})
    (base/'checks.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))
    if registry_before != registry_after:
        sys.exit(1)
else: print((base/'run.log').read_text()[-6000:])

sys.exit(0 if r.returncode == 0 and restore_code == 0 and (base/'checks-restore.json').is_file() else 1)
