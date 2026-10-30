"""Apply an exact, idempotent XBMC-360 mapping fix before rebuilding avcodec."""
from pathlib import Path
import hashlib
OLD="#define hw_thread(x) (x+1)"
NEW="/* App workers 0..2 use HW1,2,4; worker 3 uses HW3.\n * Diagnostic worker 4 shares HW5 with presentation. */\nstatic const int xenon_worker_map[5] = {1, 2, 4, 3, 5};\n#define hw_thread(x) xenon_worker_map[(x) % 5]"
PREVIOUS="/* Verified A/B control: previous HW1,2,3,4 order; diagnostic worker 4 uses HW5. */\nstatic const int xenon_worker_map[5] = {1, 2, 3, 4, 5};\n#define hw_thread(x) xenon_worker_map[(x) % 5]"
LAYOUTS={'current':NEW,'previous':PREVIOUS}
# Normalized pinned 20658be9 xb_thread.c, verified against the local dependency.
SOURCE_SHA256="a2ecc0e814e446765adaf95842b1b683e04411a72eda67c14b81af1a27a5a533"

def validate(text):
    versions=[v for v in LAYOUTS.values() if text.count(v)==1]
    if len(versions)==1 and OLD not in text:
        original=text.replace(versions[0],OLD,1)
        patched=True
    elif text.count(OLD)==1 and 'xenon_worker_map' not in text:
        original=text
        patched=False
    else:
        raise RuntimeError("Unrecognized xb_thread.c; refusing an unverified affinity patch")
    if hashlib.sha256(original.encode()).hexdigest()!=SOURCE_SHA256:
        raise RuntimeError("Unrecognized xb_thread.c version; refusing an unverified affinity patch")
    return patched

def patch(path,layout='current'):
    if layout not in LAYOUTS:raise ValueError('Unknown worker layout: '+layout)
    path=Path(path);text=path.read_text()
    patched=validate(text)
    if LAYOUTS[layout] in text:return False
    original=text
    if patched:
        for version in LAYOUTS.values():original=original.replace(version,OLD)
    path.write_text(original.replace(OLD,LAYOUTS[layout]));return True
if __name__=='__main__':
    import sys
    patch(sys.argv[1])
