from pathlib import Path
import sys
r=Path(__file__).resolve().parents[2];s=(r/'xdk/src/main_xdk.cpp').read_text()
def fn(sig):
 a=s.index(sig);b=s.index('{',a)+1;d=1
 while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
f=(r/'xdk/tests/storage_recovery_fixture.cpp').read_text().replace('../../../src/','../src/')
code='\n'.join(fn(x) for x in ['char *storage_read(', 'static void storage_log_failure(', 'bool storage_write('])
Path(sys.argv[1]).write_text(f.replace('/* ACTUAL_STORAGE */',code))
