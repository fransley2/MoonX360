"""Compile the actual YUV C string sources with the installed Xbox XDK fxc.

Compiler cycle estimates are not native GPU execution measurements.
"""
import argparse
import ast
import json
import os
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
sdk = Path(os.environ.get('XEDK', r'C:\Program Files (x86)\Microsoft Xbox 360 SDK'))
compiler = sdk / 'bin/win32/fxc.exe'
source = (root / 'xdk/src/gfx_xdk.cpp').read_text()
results = {}
for name in ('ps_yuv_src', 'ps_yuv_quality_src', 'ps_yuv_crt_src',
             'ps_yuv_smooth_src', 'ps_yuv_smooth_crt_src'):
    # A semicolon inside an HLSL literal is not the C declaration terminator.
    literal = source[source.index('static const char ' + name + '[]'):]
    literal = literal[:literal.index('";') + 1]
    hlsl = ''.join(ast.literal_eval(s) for s in re.findall(r'"(?:[^"\\]|\\.)*"', literal))
    if 'smooth' in name:
        assert all('register(s%d)' % i in hlsl for i in range(6))
        assert 'alpha*(p-c)' in hlsl and 'abs(c-p)' in hlsl
    target = args.output / name
    target.with_suffix('.hlsl').write_text(hlsl)
    run = subprocess.run([str(compiler), '/nologo', '/T', 'ps_3_0', '/E', 'main',
                          '/Fo', str(target.with_suffix('.bin')),
                          '/Fc', str(target.with_suffix('.asm')),
                          str(target.with_suffix('.hlsl'))], capture_output=True, text=True)
    target.with_suffix('.log').write_text(run.stdout + run.stderr)
    if run.returncode:
        raise SystemExit('%s: Xbox fxc failed: %s%s' % (name, run.stdout, run.stderr))
    assembly = target.with_suffix('.asm').read_text()
    results[name] = dict(exit=0, texture_samples=hlsl.count('tex2D('),
                         bytecode_bytes=target.with_suffix('.bin').stat().st_size,
                         estimates=[line for line in assembly.splitlines()
                                    if 'ALU:' in line or 'Performance (' in line])
(args.output / 'results.json').write_text(json.dumps(results, indent=2))
print('PASS: five actual YUV shaders compiled by Xbox fxc ps_3_0; GPU timing unmeasured')
