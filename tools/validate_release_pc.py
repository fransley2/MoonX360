"""Run local release checks. Never connects to a console or publishes artifacts."""
from pathlib import Path
import json
import os
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/release-candidate/evidence'
OUT.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, PYTHONIOENCODING='utf-8')
env.setdefault('XEDK', r'C:\Program Files (x86)\Microsoft Xbox 360 SDK')
checks = [
    ('host', ['xdk/tests/run_host_tests.py', '--keep-going']),
    ('python', ['-m', 'unittest', 'discover', '-s', 'xdk/tests', '-p', 'test_*.py', '-v']),
    ('expansion', ['xdk/tests/run_expansion_tests.py']),
    ('aurora', ['xdk/tests/run_aurora_launch_test.py']),
    ('artwork', ['xdk/tests/run_artwork_tests.py']),
    ('remote-feature-fixtures', ['xdk/tests/run_remote_features.py']),
    ('ui-shaders', ['xdk/tests/check_ui_shaders.py']),
    ('temporal-shaders', ['xdk/tests/compile_temporal_shaders.py', '--output', str(OUT/'temporal-shaders')]),
    ('legacy-brand-fixtures', ['xdk/tests/verify_moonx360.py']),
]
results = []
for name, args in checks:
    print('CHECK:', name, flush=True)
    started = time.monotonic()
    check_env=dict(env)
    if name=='legacy-brand-fixtures':
        check_env['MOONX_UI_BASELINE_ZIP']=str(OUT/'pre-edit-sources.zip')
    result = subprocess.run([sys.executable, *args], cwd=ROOT, env=check_env, capture_output=True)
    log = OUT/(name+'-final.log')
    log.write_bytes(result.stdout+result.stderr)
    results.append(dict(name=name, command=[sys.executable, *args], exit=result.returncode,
                        result='PASS — HOST VALIDATED' if not result.returncode else 'FAIL — REQUIRES FIX',
                        environment='Windows desktop; MSVC/XDK fxc or Python; simulated Xbox APIs',
                        seconds=round(time.monotonic()-started, 3), log=str(log.relative_to(ROOT))))
    print(results[-1]['result'], flush=True)
    if name == 'host' and (ROOT/'host-tests/results.json').exists():
        (OUT/'host-results.json').write_bytes((ROOT/'host-tests/results.json').read_bytes())
# Native code is compiled by the XDK separately; desktop /analyze can check the
# actual configuration/persistence implementation without simulated GPU claims.
vc=Path(os.environ.get('VS_VCVARS64',r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'))
setup=subprocess.run(f'cmd.exe /d /s /c ""{vc}" >nul && set"',capture_output=True,text=True,check=True)
analysis_env=dict(env)
for line in setup.stdout.splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1);analysis_env[key.upper()]=value
cl=next(str(Path(p)/'cl.exe') for p in analysis_env['PATH'].split(';') if (Path(p)/'cl.exe').exists())
command=[cl,'/nologo','/c','/TP','/analyze','/W3','/D_CRT_SECURE_NO_WARNINGS','/Dstrtok_r=strtok_s',
         '/I'+str(ROOT/'host-tests'),'/I'+str(ROOT/'xdk/deps/moonlight-common-c/src'),
         str(ROOT/'src/config.c'),'/Fo'+str(OUT/'config-analysis.obj')]
analysis=subprocess.run(command,cwd=OUT,env=analysis_env,capture_output=True)
(OUT/'static-analysis.log').write_bytes(analysis.stdout+analysis.stderr)
(OUT/'static-analysis.json').write_text(json.dumps(dict(command=command,exit=analysis.returncode,
    scope='Desktop MSVC /analyze; configuration/persistence only'),indent=2))
results.append(dict(name='static analysis',exit=analysis.returncode,result='PASS — HOST VALIDATED' if not analysis.returncode else 'FAIL — REQUIRES FIX',
                    environment='Desktop MSVC /analyze; configuration/persistence only',log='build/release-candidate/evidence/static-analysis.log'))
diff = subprocess.run(['git', 'diff', '--check'], cwd=ROOT, capture_output=True)
(OUT/'diff-check.log').write_bytes(diff.stdout+diff.stderr)
results.append(dict(name='git diff --check', exit=diff.returncode,
                    result='PASS — HOST VALIDATED' if not diff.returncode else 'FAIL — REQUIRES FIX',
                    environment='Git working tree', log='build/release-candidate/evidence/diff-check.log'))
(OUT/'pc-results.json').write_text(json.dumps(results, indent=2, ensure_ascii=False), encoding='utf-8')
raise SystemExit(1 if any(r['exit'] for r in results) else 0)
