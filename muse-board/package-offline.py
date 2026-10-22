"""Package the offline build, refusing to include the user's SDK token."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import zipfile

root = Path(__file__).resolve().parent
build = root / 'esp32/build-muse-viewe-s3-round'
out = root.parent / 'Muse-Offline-VIEWE'
out.mkdir(exist_ok=True)
cache = (build / 'CMakeCache.txt').read_text()
assert 'MUSE_OFFLINE_DEMO:UNINITIALIZED=ON' in cache or 'MUSE_OFFLINE_DEMO:BOOL=ON' in cache
app = (build / 'muse-gadget.bin').read_bytes()
assert b'Official Muse UI on VIEWE: offline demo ready.' in app, 'Not the offline application'
token_file = root / '.sdk-token'
token = token_file.read_text(encoding='utf-8-sig').strip().encode() if token_file.exists() else b''
if token and token in app:
    raise SystemExit('Refusing to package: SDK token found in application.')
files = {'bootloader/bootloader.bin': 'firmware/bootloader.bin',
         'partition_table/partition-table.bin': 'firmware/partition-table.bin',
         'ota_data_initial.bin': 'firmware/ota_data_initial.bin',
         'muse-gadget.bin': 'firmware/muse-offline.bin'}
(out / 'firmware').mkdir(exist_ok=True)
for src, dest in files.items():
    shutil.copy2(build / src, out / dest)
(out / 'flash_args').write_text('--flash-mode dio --flash-freq 80m --flash-size keep\n'
    '0x0 firmware/bootloader.bin\n0x10000 firmware/partition-table.bin\n'
    '0x1d000 firmware/ota_data_initial.bin\n0x20000 firmware/muse-offline.bin\n')
shutil.copy2(root / 'LICENSE', out / 'LICENSE')
names = subprocess.check_output(['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=root).decode().split('\0')
with zipfile.ZipFile(out / 'source.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for name in names:
        path = root / name
        if not name or not path.is_file(): continue
        contents = path.read_bytes()
        if token and token in contents: raise RuntimeError(f'Credential detected in source: {name}')
        z.writestr('muse-board/' + name, contents)
sha = {name: hashlib.sha256((out / name).read_bytes()).hexdigest() for name in files.values()}
sha['flash_args'] = hashlib.sha256((out / 'flash_args').read_bytes()).hexdigest()
manifest = {'upstream': 'https://github.com/facebookincubator/muse-gadget-sdk',
    'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root).decode().strip(),
    'board': 'VIEWE-ROUND-V1.1 CO5300 466x466 ESP32-S3 N16R8',
    'ui_input_commit': '8e613f77f3a95a63e70cf8be549db9e589105ea0',
    'mode': 'offline interactive official UI', 'sdk_token_in_binary': False, 'hardware_tested': False,
    'build_passed': True, 'app_bytes': len(app), 'sha256': sha}
(out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
(out / 'SOURCE-NOTES.txt').write_text(
    'Official upstream: ' + manifest['upstream'] + '\nCommit: ' + manifest['commit'] + '\n\n'
    'Reused directly: esp32/avatar/muse_pixel.c; muse UI, fonts, menus, settings, input and rendering.\n'
    'UI/input updated from upstream commit 8e613f77f3a95a63e70cf8be549db9e589105ea0.\n'
    'All original copyright notices and Apache-2.0 license are preserved.\n'
    'Local changes: VIEWE board port, main/muse_offline.c, UI offline presentation adapter and upstream touchscreen talk, Windows launchers.\n'
    'The official renderer was executed on the host to produce 168 preview frames.\n'
    'Firmware compiled successfully. No physical screen, touch or BOOT test was performed.\n'
    'The full upstream Windows host test suite is not a passing validation baseline; see VALIDATION.md in source.\n'
    'Settings pages use the real UI; cloud and radio operations have no running backend in this demo.\n'
    'The default character is procedural artwork, not a downloaded video or an imitation drawing.\n')
archive = root.parent / 'Muse-Offline-VIEWE.zip'
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for path in out.rglob('*'):
        if path.is_file() and '__pycache__' not in path.parts:
            z.write(path, 'Muse-Offline-VIEWE/' + path.relative_to(out).as_posix())
print(f'Offline package ready: {archive}; application {len(app)} bytes; SDK token absent.')
