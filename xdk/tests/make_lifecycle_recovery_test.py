"""Actual Guide ownership, resize ordering and GameStream launch regression cases."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
def fn(path,sig):
 s=(root/path).read_text();a=s.index(sig);b=s.index('{',a)+1;depth=1
 while depth:
  depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]+'\n'
fixture=(root/'xdk/tests/lifecycle_recovery_fixture.cpp').read_text()
for marker,path,sigs in [
 ('GUIDE','xdk/src/gfx_xdk.cpp',['D3DDevice *gfx_begin(', 'void gfx_end(', 'static void system_ui_present(']),
 ('RENDER_PUMP','xdk/src/video_xdk.cpp',['static void render_tick(', 'static DWORD WINAPI render_main(']),
 ('RESIZE','xdk/src/video_xdk.cpp',['static bool resize_video(']),
 ('LAUNCH','src/gamestream.c',['static char *xml_get_from(', 'static char *xml_get(', 'bool gs_launch('])]:
 fixture=fixture.replace('/* '+marker+' */',''.join(fn(path,s) for s in sigs))
# Full pairing depends on crypto. Check the already-paired path cannot enter the
# unpair-on-failure cleanup and failed cleanup retains the original error.
s=(root/'src/gamestream.c').read_text();pair=s[s.index('bool gs_pair('):s.index('bool gs_unpair(')]
a=pair.index('if (srv->paired)');b=pair.index('/* 1.',a)
assert 'return true;' in pair[a:b] and 'goto out' not in pair[a:b]
assert 'memcpy(last_error,failure,sizeof(last_error))' in pair
Path(sys.argv[1]).write_text(fixture)
