"""Exercise actual XDK option/callback setup and shader constants on the host."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
source = (root / 'xdk/src/video_xdk.cpp').read_text()

def function(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

fixture = (root / 'xdk/tests/session_profile_fixture.cpp').read_text()
fixture = fixture.replace('/* ACTUAL_SETUP */', '\n'.join(function(name) for name in
    ['void video_set_options(', 'int video_colorspace(', 'void video_init_callbacks(']))
start = source.index('    d = gfx_begin();', source.index('static DWORD WINAPI render_main('))
end = source.index('    gfx_end();', start) + len('    gfx_end();')
fixture = fixture.replace('/* ACTUAL_RENDER_SETUP */', source[start:end])
gfx = (root / 'xdk/src/gfx_xdk.cpp').read_text()
start = gfx.index('static const char ps_yuv_src[]')
end = gfx.index('\n\n', start)
fixture = fixture.replace('/* ACTUAL_SHADER */', gfx[start:end])
# Wiring check against the real app, including order before connecting.
app = (root / 'src/app.c').read_text()
assert 'lj.sc.colorSpace = video_colorspace(cfg.width, cfg.height);' in app
assert 'lj.sc.colorRange = COLOR_RANGE_LIMITED;' in app
for member, value in [('width','cfg.width'), ('height','cfg.height'),
                      ('target_fps','cfg.fps'), ('slice_count','cfg.slice_count'),
                      ('color_space','lj.sc.colorSpace')]:
    assert f'vo.{member} = {value};' in app
assert app.index('video_set_options(&vo);') < app.index('video_init_callbacks(&dr);')
Path(sys.argv[1]).write_text(fixture)
