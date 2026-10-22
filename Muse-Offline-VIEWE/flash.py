"""Flash the packaged offline demo without recompiling or accessing SDK tokens."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
from serial.tools import list_ports

ROOT = Path(__file__).resolve().parent

def main():
    manifest = json.loads((ROOT / 'manifest.json').read_text())
    for name, expected in manifest['sha256'].items():
        if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != expected:
            raise RuntimeError(f'Missing or changed firmware: {name}. Restore the package.')
    print('Muse offline demo - official UI on VIEWE ROUND')
    print('Uploading replaces your current firmware. No Muse account is needed.')
    while True:
        ports = [p for p in list_ports.comports() if p.vid is not None]
        candidates = [p for p in ports if (p.vid, p.pid) == (0x303A, 0x1001)]
        if len(candidates) == 1:
            port = candidates[0].device
            print(f'ESP32 USB detected: {port}')
        else:
            for p in ports:
                print(f'{p.device}: {p.description}')
            if not ports:
                print('No USB board. Connect it; hold BOOT, tap RST, release BOOT if needed.')
                if input('Enter to rescan, Q to quit: ').lower() == 'q': return 1
                continue
            port = input('Board COM port (or Q to quit): ').strip().upper()
            if port == 'Q': return 1
            if port not in [p.device.upper() for p in ports]: continue
        cmd = [sys.executable, '-m', 'esptool', '--chip', 'esp32s3', '--port', port,
               '--baud', '460800', '--before', 'default-reset', '--after', 'hard-reset',
               'write-flash', '@flash_args']
        if subprocess.call(cmd, cwd=ROOT) == 0:
            print('DONE. Tap RST if needed. The offline animation starts automatically.')
            return 0
        print('Close serial monitors. Hold BOOT, tap RST, release BOOT.')
        if input('Enter to retry, Q to quit: ').lower() == 'q': return 1

if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError) as exc:
        print(f'ERROR: {exc}')
        sys.exit(1)
