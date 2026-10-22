"""Flash only this demo's prebuilt firmware; never downloads or compiles code."""
from pathlib import Path
import hashlib,json,subprocess,sys
from serial.tools import list_ports
root=Path(__file__).resolve().parent
manifest=json.loads((root/'firmware/manifest.json').read_text())
for entry in manifest['files'].values():
    path=root/'firmware'/entry['file']
    if hashlib.sha256(path.read_bytes()).hexdigest()!=entry['sha256']:
        raise SystemExit('Firmware verification failed: '+entry['file'])
ports=[p for p in list_ports.comports() if p.vid==0x303A and p.pid==0x1001]
port=sys.argv[1] if len(sys.argv)>1 else ''
if not port:
    if len(ports)==1:port=ports[0].device
    else:
        for p in list_ports.comports():print(p.device,p.description)
        port=input('ESP32 COM port: ').strip()
if not port.upper().startswith('COM') or not port[3:].isdigit():raise SystemExit('Invalid COM port')
print('Uploading '+manifest['project']+' to '+port+'; this replaces the running demo.',flush=True)
settings=manifest['flash_settings']
cmd=[sys.executable,'-m','esptool','--chip','esp32s3','--port',port,'--baud','460800',
     '--before','default-reset','--after','hard-reset','write-flash',
     '--flash-mode',settings['flash_mode'],'--flash-freq',settings['flash_freq'],
     '--flash-size',settings['flash_size']]
for offset,entry in sorted(manifest['files'].items(),key=lambda x:int(x[0],0)):
    cmd.extend([offset,str(root/'firmware'/entry['file'])])
raise SystemExit(subprocess.call(cmd,cwd=root))
