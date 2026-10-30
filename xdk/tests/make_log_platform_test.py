"""Exercise the actual SDK log capture/checkpoint/snapshot functions with in-memory storage."""
from pathlib import Path
import sys
tests = Path(__file__).resolve().parent
source = (tests.parent / "src/main_xdk.cpp").read_text()
state = source[source.index("static struct diagnostic_ring diagnostic_log;"):source.index("/* ------------------------------------------------------------ platform layer */")]
api = source[source.index("void plat_shutdown_stage("):source.index("bool storage_init(")]
fixture = (tests / "log_platform_fixture.cpp").read_text()
Path(sys.argv[1]).write_text(fixture.replace("/* ACTUAL_STATE */", state).replace("/* ACTUAL_API */", api))
