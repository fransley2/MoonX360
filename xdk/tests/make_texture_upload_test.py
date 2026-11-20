"""Generate a host test of the actual GPU upload function; compile with MSVC.

Usage: python xdk/tests/make_texture_upload_test.py <output.cpp>
The texture stub marks GPU memory as an invalid destination for XMemCpy.
It does not emulate Xenon cache instructions or replace console validation.
"""
from pathlib import Path
import sys

tests = Path(__file__).resolve().parent
source = (tests.parent / "src/gfx_xdk.cpp").read_text(encoding="utf-8")
start = source.index("bool gfx_upload(")
end = source.index("\n}\n", start) + 3
code = (tests / "texture_upload_fixture.cpp").read_text(encoding="utf-8")
code = code.replace("/* ACTUAL_UPLOAD_FUNCTION */", source[start:end])
Path(sys.argv[1]).write_text(code, encoding="utf-8", newline="\n")
