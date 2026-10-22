"""Render previews from the official renderer's RGB565 host output."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import struct

root = Path(__file__).resolve().parent
out = root.parent / 'Muse-Offline-VIEWE' / 'preview'
out.mkdir(parents=True, exist_ok=True)
raw = (root / 'artifacts/offline-preview.rgb565').read_bytes()
assert len(raw) == 7 * 24 * 64 * 64 * 2
names = ['WAKING UP', 'READY', 'LISTENING', 'THINKING', 'SPEAKING', 'ERROR', 'GOODBYE']
font = ImageFont.truetype('C:/Windows/Fonts/consola.ttf', 17)
frames = []
sheet = Image.new('RGB', (4 * 280, 2 * 320), '#111016')
for mode, name in enumerate(names):
    for frame in range(24):
        offset = (mode * 24 + frame) * 64 * 64 * 2
        vals = struct.unpack_from('<4096H', raw, offset)
        im = Image.new('RGB', (64, 64))
        im.putdata([(((v >> 11) & 31) * 255 // 31,
                     ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31) for v in vals])
        im = im.resize((256, 256), Image.Resampling.NEAREST)
        card = Image.new('RGB', (280, 320), 'black')
        card.paste(im, (12, 32))
        d = ImageDraw.Draw(card)
        d.text((140, 16), name, fill='#d8d2ff', font=font, anchor='mm')
        frames.append(card)
        if frame == 18:
            sheet.paste(card, ((mode % 4) * 280, (mode // 4) * 320))
            card.save(out / (name.lower().replace(' ', '-') + '.png'))
d = ImageDraw.Draw(sheet)
d.text((3 * 280 + 20, 320 + 100), 'OFFICIAL MUSE\nCHARACTER RENDERER\n\nSoftware preview\n(not a device capture)', fill='#d8d2ff', font=font)
sheet.save(out / 'official-character-states.png')
frames[0].save(out / 'official-character.gif', save_all=True, append_images=frames[1:], duration=83, loop=0)
print('Rendered 168 frames from the actual upstream C renderer.')
