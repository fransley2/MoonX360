"""Execute the real menu renderer against a bounded, fence-checking D3D fixture."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
code=(root/'xdk/src/gfx_ui.cpp').read_text()
code=code[code.index('#define GLYPH_W'):]
gfx=(root/'xdk/src/gfx_xdk.cpp').read_text()
present=gfx[gfx.index('static D3DTexture *ui_tex[2];'):]
fixture=(root/'xdk/tests/gpu_ui_fixture.cpp').read_text()
Path(sys.argv[1]).write_text(fixture.replace('/* ACTUAL_RENDERER */',code).replace('/* ACTUAL_PRESENT */',present))
