"""Offline geometry check and preview, using paths extracted from the actual sketch.
This checks artwork, not the ESP32 hardware or its compiled firmware.
"""
import re, math, json
from pathlib import Path
from PIL import Image, ImageDraw, ImageChops

ROOT = Path(__file__).resolve().parents[1]
text = (ROOT / 'viewe_touch_rings.ino').read_text()
art = text.split('void buildArtwork() {', 1)[1].split('// Invert the CSS', 1)[0]
shapes = []
for match in re.finditer(r'addPath\("([^"]+)"\s*,(\d),(\d+),(CYAN|YELLOW)\)', art):
    d, layer, period, color = match.groups()
    tokens = re.findall(r'[MLCHVZ]|-?\d+(?:\.\d+)?', d)
    i = 0; contours = []; pts = []; current = (0,0)
    def value():
        global i
        f = float(tokens[i]); i += 1; return f
    def point(): return (value()-172, value()-172)
    while i < len(tokens):
        op = tokens[i]; i += 1
        if op == 'M':
            if pts: contours.append(pts)
            current = point(); pts = [current]
        elif op in ('L','H','V'):
            current = point() if op=='L' else (value()-172,current[1]) if op=='H' else (current[0],value()-172)
            pts.append(current)
        elif op=='C':
            a=current; b=point(); c=point(); e=point()
            steps=max(4,math.ceil((math.dist(a,b)+math.dist(b,c)+math.dist(c,e))/3))
            for j in range(1,steps+1):
                t=j/steps; u=1-t
                current=tuple(u**3*a[k]+3*u*u*t*b[k]+3*u*t*t*c[k]+t**3*e[k] for k in (0,1))
                pts.append(current)
        elif op=='Z':
            contours.append(pts); pts=[]
        else: raise AssertionError(op)
    if pts: contours.append(pts)
    shapes.append(dict(contours=contours,layer=int(layer),period=int(period),color=color,center=False))

for m in re.finditer(r'addBand\(([^;]+)\);', art):
    a=[x.strip() for x in m[1].split(',')]
    r,w,start,end=[float(x.rstrip('f')) for x in a[:4]]
    steps=max(8,math.ceil((end-start)/2))
    pts=[]
    for side in range(2):
        radius=r+(-w if side else w)/2
        for j in range(steps+1):
            i=steps-j if side else j
            angle=math.radians(start+(end-start)*i/steps)
            pts.append((radius*math.cos(angle),radius*math.sin(angle)))
    shapes.append(dict(contours=[pts],layer=int(a[4]),period=float(a[5]),color=a[6],center=len(a)>7))

assert len(shapes)==15, len(shapes)
shapes.sort(key=lambda s:-s['layer'])

def ease(x, x1=.42,y1=0,x2=.58,y2=1):
    if x<=0:return 0
    if x>=1:return 1
    lo,hi=0,1
    for _ in range(12):
        t=(lo+hi)/2; u=1-t
        bx=3*u*u*t*x1+3*u*t*t*x2+t*t*t
        if bx<x:lo=t
        else:hi=t
    return 3*u*u*t*y1+3*u*t*t*y2+t*t*t

def points(shape, seconds, hover):
    p=shape['period']; phase=(seconds%(2*p))/p if p else 0
    angle=2*math.pi*ease(2-phase if phase>1 else phase)
    ca,sa=math.cos(angle),math.sin(angle)
    tilt=math.radians(-80*hover); ct,st=math.cos(tilt),math.sin(tilt)
    skew=math.tan(math.radians(30*hover)); scale=1.8+(1.36-1.8)*hover
    offset=[0,1,0,1,2][shape['layer']]
    contours=[]
    for contour in shape['contours']:
        out=[]
        for x,y in contour:
            x,y=ca*x-sa*y,sa*x+ca*y
            if shape['center']:
                a=math.radians(-30*hover); x+=45*hover; y-=3*hover
                x,y=math.cos(a)*x-math.sin(a)*y,math.sin(a)*x+math.cos(a)*y
            x+=45*offset*hover; y-=35*offset*hover; x+=skew*y
            out.append((233+scale*(ct*x-st*y),233+scale*(st*x+ct*y)))
        contours.append(out)
    return contours

max_radius=0
for sec in range(168):
    for hover in (0,.25,.5,.75,1):
        for s in shapes:
            for contour in points(s,sec,hover):
                max_radius=max(max_radius,max(math.hypot(x-233,y-233) for x,y in contour))
assert max_radius<233, max_radius

previews=[]
for hover,name in [(0,'idle'),(1,'touch')]:
    scale=3; image=Image.new('RGB',(466*scale,466*scale),'black')
    for s in shapes:
        mask=Image.new('1',image.size)
        for contour in points(s,1.6,hover):
            part=Image.new('1',image.size)
            ImageDraw.Draw(part).polygon([(round(x*scale),round(y*scale)) for x,y in contour],fill=1)
            mask=ImageChops.logical_xor(mask,part)
        image.paste('#00ffff' if s['color']=='CYAN' else '#ffff00',(0,0),mask)
    image=image.resize((466,466),Image.Resampling.LANCZOS)
    previews.append(image)
canvas=Image.new('RGB',(932,506),'#141b22')
draw=ImageDraw.Draw(canvas)
for i,(im,label) in enumerate(zip(previews,['IDLE','TOUCH HELD'])):
    canvas.paste(im,(466*i,40)); draw.text((466*i+20,14),label,fill='white')
canvas.save(ROOT/'preview.png')

report={'shapes':len(shapes),'maximum_radius_pixels':round(max_radius,3),
        'available_radius_pixels':233,'sampled_seconds':168,
        'transition_positions':[0,.25,.5,.75,1],
        'hardware_tested':False,'full_arduino_compile':False}
(ROOT/'research'/'artwork-check.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
