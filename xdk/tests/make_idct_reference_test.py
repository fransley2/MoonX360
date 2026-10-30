"""Generate a host C comparison of current VMX kernels and original FFmpeg C.

Usage: python xdk/tests/make_idct_reference_test.py <ffmpeg-root> <output.c>
Compile output.c with desktop MSVC, then run the resulting test executable.
The host model checks arithmetic and coefficient side effects, not Xenon timing.
"""
from pathlib import Path
import sys

tests = Path(__file__).resolve().parent
source = (tests.parent / "ffmpeg_xbox/h264_vmx.c").read_text(encoding="utf-8")
ffmpeg = (Path(sys.argv[1]) / "libavcodec/h264idct_template.c").read_text(encoding="utf-8")
constants = source[source.index("static const __declspec(align(16)) uint8_t perm_mrghh"):
                   source.index("/* ------------------------------------------------------------ stores */")]
kernels = source[source.index("static __forceinline void clear_block16"):
                 source.index("/* ------------------------------------------------------------ loop filter */")]
reference = ffmpeg[ffmpeg.index("void FUNCC(ff_h264_idct_add)"):
                   ffmpeg.index("/**\n * IDCT transforms")]
output = (tests / "vmx_idct_emulation.h").read_text(encoding="utf-8")
output += constants + kernels + reference
output += (tests / "idct_reference_cases.c").read_text(encoding="utf-8")
Path(sys.argv[2]).write_text(output, encoding="utf-8", newline="\n")
