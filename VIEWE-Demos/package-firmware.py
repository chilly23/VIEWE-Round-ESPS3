"""Copy verified build payloads into small, ready-to-flash project packages."""
from pathlib import Path
import hashlib,json,shutil,sys
root=Path(__file__).resolve().parent.parent
for name in sys.argv[1:]:
    project=root/name
    build=project/'build'
    data=json.loads((build/'flasher_args.json').read_text())
    out=project/'firmware'
    out.mkdir(exist_ok=True)
    files={}
    for offset,relative in data['flash_files'].items():
        src=build/relative
        dest=out/Path(relative).name
        shutil.copy2(src,dest)
        files[offset]={'file':dest.name,'sha256':hashlib.sha256(dest.read_bytes()).hexdigest()}
    manifest={'board':'VIEWE-ROUND-V1.1 / CO5300 / ESP32-S3 N16R8',
              'project':name,'flash_settings':data['flash_settings'],'files':files}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    shutil.copy2(root/'VIEWE-Demos/flash-demo.py',project/'flash-demo.py')
    print(name+': packaged '+str(len(files))+' firmware segments')
