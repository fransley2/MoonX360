"""Exercise the actual SDK stop/free functions with strict CPU/GPU lifetime stubs."""
from pathlib import Path
import sys

tests = Path(__file__).resolve().parent
source = (Path(sys.argv[2]) if len(sys.argv) > 2 else tests.parent / "src/video_xdk.cpp").read_text()
def function(name):
    start = source.index("static void " + name + "(")
    body = source.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"

fixture = (tests / "video_teardown_fixture.cpp").read_text()
Path(sys.argv[1]).write_text(fixture.replace("/* ACTUAL_FUNCTIONS */", function("video_start") + function("video_stop") + function("free_video_buffers") + function("free_resources")))
