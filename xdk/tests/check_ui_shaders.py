"""Compile the actual original UI shader strings with the installed Xbox FXC."""
import ast,json,os,re,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[2]
out=root/'host-tests/native/nxe';out.mkdir(exist_ok=True,parents=True)
source=(root/'xdk/src/gfx_ui.cpp').read_text()
body=source[source.index('static const char ui_shader[]='):source.index('static ID3DXBuffer *compile_ui')]
shader=out/'ui.hlsl'
shader.write_text(''.join(ast.literal_eval(s) for s in re.findall(r'"(?:[^"\\]|\\.)*"',body)))
fxc=Path(os.environ['XEDK'])/'bin/win32/fxc.exe'
results={}
for entry,profile in [('vertex','vs_3_0'),('sample_ui','ps_3_0'),('shape','ps_3_0')]:
    assembly=out/(entry+'.txt')
    run=subprocess.run([str(fxc),'/nologo','/T',profile,'/E',entry,'/Fo',str(out/(entry+'.bin')),
        '/Fc',str(assembly),str(shader)],capture_output=True,text=True)
    output=run.stdout+run.stderr
    assert run.returncode==0 and 'warning ' not in output,output
    text=assembly.read_text()
    results[entry]={'profile':profile,'texture_fetches':len(re.findall(r'\btfetch2D\b',text)),
        'compiler_estimate':re.findall(r'// ALU:[^\n]+',text)}
    print(entry,'PASS; no warnings')
(out/'shader-audit.json').write_text(json.dumps(results,indent=2))
