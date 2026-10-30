from pathlib import Path
import sys
r=Path(__file__).resolve().parents[2]
s=(r/'xdk/src/input_xdk.cpp').read_text();s=s[s.index('static struct input_settings'):]
fixture=(r/'xdk/tests/controller_hotplug_fixture.cpp').read_text()
Path(sys.argv[1]).write_text(fixture.replace('/* ACTUAL_INPUT */',s))
