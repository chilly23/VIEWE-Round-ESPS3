"""Create a source ZIP, explicitly refusing to include the private SDK token."""
from pathlib import Path
import subprocess
import zipfile

root = Path(__file__).resolve().parent
names = subprocess.check_output(
    ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=root
).decode('utf-8').split('\0')
token_path = root / '.sdk-token'
token = token_path.read_bytes().strip() if token_path.exists() else b''
files = [name for name in names if name and (root / name).is_file()]
for name in files:
    if token and token in (root / name).read_bytes():
        raise SystemExit('Refusing to package: a source file contains the SDK token.')
dest = root / 'artifacts'
dest.mkdir(exist_ok=True)
with zipfile.ZipFile(dest / 'Muse-VIEWE-source.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for name in files:
        z.write(root / name, 'muse-board/' + name)
print('Created artifacts/Muse-VIEWE-source.zip (source only; no SDK token or build binaries).')
