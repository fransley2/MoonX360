from pathlib import Path
import sys
tests = Path(__file__).resolve().parent
source = (tests.parent / "src/gfx_xdk.cpp").read_text()
code = source[source.index("static D3DTexture *ui_tex[2];"):source.index("void ui_present(void)")]
fixture = (tests / "ui_release_fixture.cpp").read_text()
Path(sys.argv[1]).write_text(fixture.replace("/* ACTUAL_UI_RELEASE */",code))
