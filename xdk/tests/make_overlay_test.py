"""Test the actual SDK telemetry formatter and overlay GPU lifetime with stubs."""
from pathlib import Path
import sys

tests = Path(__file__).resolve().parent
gfx = (tests.parent / "src/gfx_xdk.cpp").read_text()
video = (tests.parent / "src/video_xdk.cpp").read_text()
overlay = gfx[gfx.index("static canvas_t overlay_canvas;"):gfx.index("/* Two textures, so the CPU fills one")]
start = video.index("static void overlay_line(")
end = video.index("\n}\n", video.index("static bool overlay_text(")) + 3
formatter = video[start:end]
fixture = (tests / "overlay_fixture.cpp").read_text()
Path(sys.argv[1]).write_text(fixture.replace("/* ACTUAL_OVERLAY */", overlay).replace("/* ACTUAL_FORMATTER */", formatter))
