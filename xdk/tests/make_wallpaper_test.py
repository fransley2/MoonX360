"""Exercise the actual image selection handler and bounded decoder with real PNG/JPEG."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
s=(root/'src/app.c').read_text(encoding='utf-8-sig')
a=s.index('static void choose_wallpaper(void)\n{');b=s.index('\nstatic void aurora_main_path_selected',a)
fixture=(root/'xdk/tests/wallpaper_fixture.cpp').read_text()
Path(sys.argv[1]).write_text(fixture.replace('/* ACTUAL_SELECT */',s[a:b]).replace('../third_party/',(root/'third_party').as_posix()+'/'))
