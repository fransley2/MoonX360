from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
s=(root/'xdk/src/gfx_xdk.cpp').read_text()
def function(signature):
    start=s.index(signature);end=s.index('{',start)+1;depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]
fixture=(root/'xdk/tests/display_fixture.cpp').read_text()
fixture=fixture.replace('/* ACTUAL_INIT */',function('bool gfx_init('))
fixture=fixture.replace('/* ACTUAL_RECT */',function('void gfx_draw_rect('))
Path(sys.argv[1]).write_text(fixture)
