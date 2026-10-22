"""Build the VIEWE Muse port without putting SDK tokens in arguments or logs."""
from pathlib import Path
import argparse
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
PROJECT = ROOT / 'esp32'
BUILD = PROJECT / 'build-muse-viewe-s3-round'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['build', 'flash', 'monitor', 'menuconfig', 'ports'])
    parser.add_argument('--port', default='')
    parser.add_argument('--offline', action='store_true', help='Build the local official-UI demo')
    args = parser.parse_args()
    if args.action == 'ports':
        from serial.tools import list_ports
        ports = [p for p in list_ports.comports() if p.vid is not None]
        if not ports:
            print('No USB serial device found. Connect the powered VIEWE with a USB data cable.')
            return 2
        for p in ports:
            print(f'{p.device}: {p.description} (USB {p.vid:04x}:{p.pid:04x})')
        return 0
    if args.action in ('flash', 'monitor') and not args.port:
        parser.error('Specify the connected board port, for example -Port COM5 in viewe.ps1.')
    idf = Path(os.environ.get('IDF_PATH', Path.home() / 'esp' / 'esp-idf-v6.0.1'))
    if not (idf / 'tools/idf.py').is_file():
        parser.error('ESP-IDF v6.0.1 is missing. Run viewe.ps1 from PowerShell.')
    version = subprocess.check_output(
        [sys.executable, str(idf / 'tools/idf.py'), '--version'], text=True
    ).strip()
    if not re.search(r'ESP-IDF v6\.0\.1(?:\s|$)', version):
        parser.error('This firmware requires ESP-IDF v6.0.1. Select that installation with IDF_PATH.')
    token_file = ROOT / '.sdk-token'
    token = token_file.read_text(encoding='utf-8-sig').strip() if token_file.is_file() and not args.offline else ''
    if not args.offline and not re.fullmatch(r'mgst_[A-Za-z0-9._~-]+', token):
        parser.error('Run Set Token.cmd first. Do not paste the token into chat.')
    BUILD.mkdir(exist_ok=True)
    token_config = BUILD / ('sdkconfig.offline-token' if args.offline else 'sdkconfig.token')
    token_line = f'CONFIG_GADGET_SDK_TOKEN="{token}"'
    if not token_config.exists() or token_config.read_text(encoding='utf-8') != token_line + '\n':
        token_config.write_text(token_line + '\n', encoding='utf-8')
    # Existing per-build config overrides all defaults. Update only this key.
    config = BUILD / 'sdkconfig'
    if config.is_file() and not args.offline:
        text = config.read_text(encoding='utf-8')
        existing = re.search(r'^CONFIG_GADGET_SDK_TOKEN=.*$', text, flags=re.M)
        if not existing or existing.group(0) != token_line:
            text = re.sub(r'^CONFIG_GADGET_SDK_TOKEN=.*\n?', '', text, flags=re.M)
            config.write_text(text.rstrip() + '\n' + token_line + '\n', encoding='utf-8')
    defaults = ';'.join(['sdkconfig.defaults', 'devices/sdkconfig.muse',
                         'devices/sdkconfig.muse-viewe-s3-round', token_config.as_posix()])
    cmd = [sys.executable, str(idf / 'tools/idf.py'), '-B', str(BUILD),
           '-DMUSE_OFFLINE_DEMO=' + ('ON' if args.offline else 'OFF'),
           '-DIDF_TARGET=esp32s3', '-DSDKCONFIG=' + config.as_posix(),
           '-DSDKCONFIG_DEFAULTS=' + defaults]
    if args.port:
        cmd += ['-p', args.port]
    cmd += [args.action]
    if args.action in ('menuconfig', 'monitor'):
        return subprocess.call(cmd, cwd=PROJECT)
    # Build logs can contain Kconfig values on warnings. Redact before writing.
    with (ROOT / ('viewe-' + args.action + '.log')).open('w', encoding='utf-8') as log:
        p = subprocess.Popen(cmd, cwd=PROJECT, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, encoding='utf-8', errors='replace')
        for line in p.stdout:
            safe = re.sub(r'mgst_[A-Za-z0-9._~-]+', '[SDK TOKEN REDACTED]', line)
            log.write(safe)
            log.flush()
            print(safe, end='', flush=True)
        return p.wait()


if __name__ == '__main__':
    raise SystemExit(main())
