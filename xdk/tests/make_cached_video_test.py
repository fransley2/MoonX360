"""Actual cached module, upload implementations and decoder fallback wrapper.
SDK stubs check lifetime and access contracts; they DO NOT model cache coherency.
"""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
def function(source,signature):
    start=source.index(signature);end=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
gfx=(root/'xdk/src/gfx_xdk.cpp').read_text()
module=(root/'xdk/src/video_cached.cpp').read_text()
video=(root/'xdk/src/video_xdk.cpp').read_text()
fixture=(root/'xdk/tests/cached_video_fixture.cpp').read_text()
fixture=fixture.replace('/* ACTUAL_UPLOAD */','\n'.join(function(gfx,s) for s in
    ['bool gfx_upload(', 'bool gfx_upload_timed(', 'bool gfx_cached_upload(']))
fixture=fixture.replace('/* ACTUAL_MODULE */',module[module.index('static D3DTexture *cached_textures'):])
fixture=fixture.replace('/* ACTUAL_PUBLISH */',function(video,'static void publish_frame('))
fixture=fixture.replace('/* ACTUAL_PRESENT */',function(video,'static DWORD present_video('))
fixture=fixture.replace('/* ACTUAL_FALLBACK */',function(video,'static void activate_legacy_fallback('))
Path(sys.argv[1]).write_text(fixture)
