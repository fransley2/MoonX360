"""Export the supplied editable MoonX360 SVG to transparent PNGs and UI ARGB.

Requires Pillow and resvg-py on the build PC, with no runtime dependency.
The SVG is the canonical input and is never overwritten by this script.
"""
from pathlib import Path
import io
from PIL import Image
import resvg_py

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / 'assets/brand'
MASTER = DEST / 'MoonX360.svg'
for size in (1024,512,256,128,64,32):
    data = resvg_py.svg_to_bytes(svg_path=str(MASTER),width=size*4,height=size*4,skip_system_fonts=True)
    im = Image.open(io.BytesIO(data)).convert('RGBA')
    im.resize((size,size),Image.Resampling.LANCZOS).save(DEST/f'MoonX360-{size}.png')

header='/* Generated from assets/brand/MoonX360.svg. 128x128 straight-alpha ARGB; 64 KiB. */\n#pragma once\n'
pixels=Image.open(DEST/'MoonX360-128.png').convert('RGBA')
header+='static const uint32_t moonx360_icon[128*128] = {\n'
rgba=pixels.tobytes()
values=[(a<<24)|(r<<16)|(g<<8)|b for r,g,b,a in zip(rgba[0::4],rgba[1::4],rgba[2::4],rgba[3::4])]
for i in range(0,len(values),8):header+='    '+','.join('0x%08Xu'%v for v in values[i:i+8])+',\n'
header+='};\n'
(ROOT/'src/ui/brand.h').write_text(header,encoding='ascii')
print('Supplied SVG exported to six transparent PNG sizes and embedded 128px ARGB icon.')
