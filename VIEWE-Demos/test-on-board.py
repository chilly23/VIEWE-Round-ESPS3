"""Upload one built demo and capture its own firmware's health output."""
from pathlib import Path
import json,re,subprocess,sys,time
import serial
from serial.tools import list_ports
root=Path(__file__).resolve().parent.parent
allowed={'02_lvgl_demo_v9','03_esp_brookesia','04_Immersive_block','05_Spec_Analyzer','esp-mosaico-fluidbox-main'}
name=sys.argv[1]
if name not in allowed:raise SystemExit('Unknown project')
project=root/name
ports=[p for p in list_ports.comports() if p.vid==0x303A and p.pid==0x1001]
if len(ports)!=1:raise SystemExit('Expected one ESP32 native USB device')
port=ports[0].device
with (project/'flash-test.log').open('w',encoding='utf-8') as log:
    result=subprocess.run([sys.executable,str(project/'flash-demo.py'),port],stdout=log,stderr=subprocess.STDOUT)
if result.returncode:raise SystemExit('Upload failed; see flash-test.log')
received=bytearray()
deadline=time.monotonic()+40
s=None
while time.monotonic()<deadline:
    try:
        if s is None:
            s=serial.Serial()
            s.port=port;s.baudrate=115200;s.timeout=.25;s.dtr=False;s.rts=False
            s.open()
        received.extend(s.read(8192))
    except serial.SerialException:
        if s:
            s.close();s=None
        time.sleep(.3)
if s:s.close()
text=received.decode('utf-8',errors='replace')
(project/'runtime-test.log').write_text(text,encoding='utf-8')
bad=bool(re.search(r'Guru Meditation|panic\b|abort\(\)|Brownout|stack overflow|assert failed|Main:.*failed|BS:.*Invalid',text,re.I))
marker='running:' if name=='esp-mosaico-fluidbox-main' else 'display alive:'
health=text.count(marker)
bands=[int(v) for v in re.findall(r"display alive: (\d+) bands",text)]
rendered=bool(re.search(r"running: (?!0(?:\.0)? fps)\d",text)) if name=="esp-mosaico-fluidbox-main" else bool(bands and max(bands)>0)
state={'project':name,'build_passed':True,'flash_passed':True,'runtime_health_messages':health,
       'runtime_panics_detected':bad,'boot_test_passed':health>=2 and rendered and not bad,
       'render_activity_detected':rendered,
       'physical_screen_touch_verified':False}
if name=='05_Spec_Analyzer':
    peaks=[int(v) for v in re.findall(r"microphone: \d+ bytes, peak (\d+)",text)]
    state['audio_signal_detected']=bool(peaks and max(peaks)>0)
    state['application_test_passed']=state['boot_test_passed'] and state['audio_signal_detected']
statefile=root/'VIEWE-Demos/VALIDATION.json'

data=json.loads(statefile.read_text()) if statefile.exists() else {}
data[name]=state
statefile.write_text(json.dumps(data,indent=2)+'\n')
print(json.dumps(state),flush=True)
print('\n'.join(line for line in text.splitlines() if 'running:' in line or 'display alive:' in line or 'QMI' in line or 'microphone:' in line)[-2000:])
if not state['boot_test_passed'] or state.get('application_test_passed') is False:raise SystemExit(1)
