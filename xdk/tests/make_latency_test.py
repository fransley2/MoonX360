"""Exercise actual render selection and input pacing with deterministic waits."""
from pathlib import Path
import sys
root = Path(__file__).resolve().parents[2]
def extract(path, signature):
    s = (root / path).read_text()
    a = s.index(signature)
    b = s.index("{", a); end = b + 1; depth = 1
    while depth:
        depth += (s[end] == "{") - (s[end] == "}")
        end += 1
    return s[a:end]
s = (root / 'xdk/tests/latency_fixture.cpp').read_text()
s = s.replace('/* ACTUAL_DRAW */', extract('xdk/src/video_xdk.cpp', 'static bool draw_frame('))
s = s.replace('/* ACTUAL_PUBLISH */', extract('xdk/src/video_xdk.cpp', 'static void publish('))
s = s.replace('/* ACTUAL_INPUT */', extract('xdk/src/input_xdk.cpp', 'static DWORD WINAPI input_stream_main('))
Path(sys.argv[1]).write_text(s)
