"""Builds XBMC-360's Xbox 360 FFmpeg (1.2.12 with VMX128 H.264 code) with the
Xbox 360 SDK compiler, using the source lists and settings from its own
Visual Studio projects. Produces avcodec.lib, avutil.lib and pthreads.lib.

    python xdk/scripts/build_ffmpeg.py
Environment: XEDK (default: the extracted SDK under C:\\AI\\xbox\\dev kits).
"""
import concurrent.futures
import glob
import hashlib
import json
import os
import re
import subprocess
import sys
from patch_worker_map import patch as patch_worker_map
from slice_dispatch import MODES as SLICE_DISPATCH_MODES, write_generated, generate

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))          # xdk/
FFMPEG = os.path.abspath(os.environ.get('XDK_FFMPEG_ROOT',os.path.join(ROOT, "deps", "XBMC-360", "libraries", "ffmpeg")))
WORKER_MAP=os.environ.get('XDK_WORKER_MAP','current')
if WORKER_MAP not in ('current','previous'):raise SystemExit('XDK_WORKER_MAP must be current or previous')
SLICE_DISPATCH=os.environ.get('XDK_SLICE_DISPATCH','legacy')
if SLICE_DISPATCH not in SLICE_DISPATCH_MODES:raise SystemExit('XDK_SLICE_DISPATCH must be legacy or batched')
VCPROJ = os.path.join(FFMPEG, "vcproj")
OUT = os.path.join(os.path.abspath(os.environ.get("XDK_BUILD_DIR", os.path.join(ROOT, "build"))), "ffmpeg")
XEDK = os.environ.get("XEDK", r"C:\AI\xbox\dev kits\XBOX360 SDK 21256.3\extracted\XDK")
JUL12 = os.path.join(XEDK, "TechPreview", "Jul12Compiler")

ENV = dict(os.environ)
ENV["PATH"] = os.pathsep.join([os.path.join(JUL12, "bin", "win32"), os.path.join(XEDK, "bin", "win32"), ENV["PATH"]])
ENV["INCLUDE"] = os.pathsep.join([os.path.join(JUL12, "include", "xbox"), os.path.join(XEDK, "include", "xbox")])
ENV["LIB"] = os.pathsep.join([os.path.join(JUL12, "lib", "xbox"), os.path.join(XEDK, "lib", "xbox")])

# Windows' CreateProcess ignores the child's PATH, so call the tools by path.
CL = os.path.join(JUL12, "bin", "win32", "cl.exe")
LIBEXE = os.path.join(JUL12, "bin", "win32", "lib.exe")

# /Ou: instruction prescheduling for Xenon's in-order cores. /Oc: no
# divide-by-zero trap checks around integer divisions.
PROFILE = os.environ.get("XDK_OPTIMIZATION", "aggressive")
if PROFILE not in ("aggressive", "compatible"):
    raise SystemExit("XDK_OPTIMIZATION must be aggressive or compatible")
OPTIMIZATION = ["/Ox", "/Ou", "/Oc"] if PROFILE == "aggressive" else ["/O2"]
COMMON = ["/nologo", "/c"] + OPTIMIZATION + ["/Oi", "/GF", "/Gy", "/MT", "/W3", "/wd4305", "/TC",
          "/D", "WIN32", "/D", "NDEBUG", "/D", "_WINDOWS", "/D", "_XBOX"]

# Our VMX functions (xdk/ffmpeg_xbox), built into avcodec.lib.
XBOX_DSP = os.path.join(ROOT, "ffmpeg_xbox")
EXTRA_SOURCES = {"avcodec": [os.path.join(XBOX_DSP, "h264_vmx.c")]}

# Hooks at the end of FFmpeg's DSP init functions (after the per-arch ones)
# that install the VMX functions. (file, exact text, replacement)
HOOK_FIXUPS = []
for _name, _ctx, _args, _call in (
        ("h264qpel", "H264QpelContext *c, int bit_depth", "c, bit_depth", "    if (ARCH_X86)\n        ff_h264qpel_init_x86(c, bit_depth);\n"),
        ("h264chroma", "H264ChromaContext *c, int bit_depth", "c, bit_depth", "    if (ARCH_X86)\n        ff_h264chroma_init_x86(c, bit_depth);\n"),
        ("h264dsp", "H264DSPContext *c, const int bit_depth, const int chroma_format_idc", "c, bit_depth, chroma_format_idc", "    if (ARCH_X86)\n        ff_h264dsp_init_x86(c, bit_depth, chroma_format_idc);\n"),
        ("videodsp", "VideoDSPContext *ctx, int bpc", "ctx, bpc", "    if (ARCH_X86)\n        ff_videodsp_init_x86(ctx, bpc);\n")):
    _decl = f"void ff_{_name}_init_x86({_ctx});\n"
    HOOK_FIXUPS.append((f"libavcodec/{_name}.h", _decl, _decl + f"void ff_{_name}_init_xbox({_ctx});\n"))
    HOOK_FIXUPS.append((f"libavcodec/{_name}.c", _call,
                        _call + f"#ifdef _XBOX\n    ff_{_name}_init_xbox({_args});\n#endif\n"))


def apply_fixups(fixups):
    """Applies text replacements to FFmpeg's sources; already-applied ones are skipped."""
    for rel, old, new in fixups:
        path = os.path.join(FFMPEG, rel)
        text = open(path, encoding="utf-8", errors="surrogateescape", newline="").read()
        crlf = "\r\n" in text
        if crlf:
            old, new = old.replace("\n", "\r\n"), new.replace("\n", "\r\n")
        if new in text or "_init_xbox(" in text:
            continue
        if old in text:
            updated = text.replace(old, new, 1)
        else:
            # Whitespace-tolerant fallback for multi-line declarations/calls (e.g. h264dsp)
            fn_m = re.search(r"ff_\w+_init_x86", old)
            xbox_lines = [l for l in new.splitlines(True) if "xbox" in l or "#ifdef" in l or "#endif" in l]
            pattern = rf"([^\r\n]*{fn_m.group(0)}\s*\([^;]+;\s*\r?\n)" if fn_m else None
            if not pattern or not re.search(pattern, text):
                raise SystemExit(f"fixup no longer matches: {rel}")
            updated = re.sub(pattern, r"\1" + "".join(xbox_lines), text, count=1)
        with open(path, "w", encoding="utf-8", errors="surrogateescape", newline="") as f:
            f.write(updated)
# The FFmpeg libraries need HAVE_AV_CONFIG_H (pulls in libavutil/internal.h);
# XBMC-360's "Release" configuration sets it, its "Release_LTCG" one doesn't.
FFMPEG_DEFINES = ["HAVE_AV_CONFIG_H"]

LIBS = {
    # name: (vcxproj, extra include dirs relative to the project dir, forced includes, extra defines)
    "avutil": ("libavutil/libavutil.vcxproj", [r"..\external\include", r"..\.."], ["config.h"], FFMPEG_DEFINES),
    "avcodec": ("libavcodec/libavcodec.vcxproj", [r"..\..", r"..\external\zlib", r"..\external\include"],
                [r"libavutil/intmath.h", "config.h"], FFMPEG_DEFINES),
    "pthreads": ("pthreads/pthreads.vcxproj", [r"../external/pthreads/"], [], ["HAVE_CONFIG_H", "PTW32_STATIC_LIB", "_LIB"]),
}


def sources_of(vcxproj):
    text = open(vcxproj, encoding="utf-8", errors="replace").read()
    return [s for s in re.findall(r'<ClCompile Include="([^"]+)"', text) if s.lower().endswith(".c")]


def compile_one(args):
    src, obj, flags = args
    if os.path.exists(obj) and os.path.getmtime(obj) >= os.path.getmtime(src):
        return src, 0, ""
    r = subprocess.run([CL] + flags + [src, "/Fo" + obj], env=ENV, capture_output=True, text=True,
                       errors="replace", cwd=os.path.dirname(obj))
    with open(obj + '.log', 'w', encoding='utf-8') as f:
        f.write(r.stdout + r.stderr)
    if r.returncode and os.path.exists(obj):
        os.remove(obj)
    return src, r.returncode, r.stdout + r.stderr


def build(name):
    proj, incs, forced, defines = LIBS[name]
    proj_path = os.path.join(VCPROJ, proj)
    proj_dir = os.path.dirname(proj_path)
    obj_dir = os.path.join(OUT, name)
    os.makedirs(obj_dir, exist_ok=True)

    flags = list(COMMON)
    for inc in incs:
        flags += ["/I", os.path.normpath(os.path.join(proj_dir, inc))]
    for f in forced:
        flags += ["/FI" + f]
    for d in defines:
        flags += ["/D", d]

    if name == "avcodec":
        flags += ["/I", XBOX_DSP]

    # Objects are only rebuilt when their source is newer, so start over when
    # the flags change.
    stamp = os.path.join(obj_dir, "flags.txt")
    headers = sorted(glob.glob(os.path.join(FFMPEG, '**', '*.h'), recursive=True)
                     + glob.glob(os.path.join(XBOX_DSP, '*.h')))
    signature = json.dumps([CL, flags, ENV['INCLUDE'],
                            [(p, os.path.getmtime(p)) for p in headers]])
    if not os.path.exists(stamp) or open(stamp).read() != signature:
        for f in os.listdir(obj_dir):
            if f.endswith(".obj"):
                os.remove(os.path.join(obj_dir, f))
        with open(stamp, "w") as f:
            f.write(signature)

    jobs, objs = [], []
    scheduler_stamp=None
    sources = [os.path.normpath(os.path.join(proj_dir, s)) for s in sources_of(proj_path)]
    for src in sources + EXTRA_SOURCES.get(name, []):
        # Object names must be unique across sub-directories (e.g. two dsputil.c).
        rel = os.path.relpath(src, FFMPEG if src.startswith(FFMPEG) else ROOT).replace("\\", "_").replace("/", "_")
        obj = os.path.join(obj_dir, os.path.splitext(rel)[0] + ".obj")
        if name=='avcodec' and os.path.basename(src)=='xb_thread.c':
            compiled=str(write_generated(src,os.path.join(OUT,'generated','xb_thread_batched.c'))) if SLICE_DISPATCH=='batched' else src
            unit_flags=flags+['/I',os.path.dirname(src)] if SLICE_DISPATCH=='batched' else flags
            with open(compiled,encoding='utf-8') as f:unit_hash=hashlib.sha256(f.read().encode('utf-8')).hexdigest()
            stamp_path=obj+'.dispatch.json'
            signature=json.dumps([SLICE_DISPATCH,unit_hash,unit_flags])
            if os.path.exists(obj) and (not os.path.exists(stamp_path) or open(stamp_path).read()!=signature):
                os.remove(obj)
            scheduler_stamp=(stamp_path,signature)
            jobs.append((compiled,obj,unit_flags))
        else:
            jobs.append((src, obj, flags))
        objs.append(obj)

    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        for src, rc, out in pool.map(compile_one, jobs):
            if rc != 0:
                failed += 1
                print(f"FAILED {os.path.relpath(src, FFMPEG)}")
                print("\n".join(l for l in out.splitlines() if "error" in l.lower())[:3000])
    print(f"{name}: {len(jobs)} sources, {failed} failed")
    if failed:
        return False
    if scheduler_stamp:
        with open(scheduler_stamp[0],'w') as f:f.write(scheduler_stamp[1])

    lib = os.path.join(OUT, name + ".lib")
    rsp = os.path.join(OUT, name + ".rsp")
    with open(rsp, "w") as f:
        f.write("\n".join(f'"{o}"' for o in objs))
    r = subprocess.run([LIBEXE, "/nologo", "/OUT:" + lib, "@" + rsp], env=ENV, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout + r.stderr)
        return False
    print(f"  -> {lib} ({os.path.getsize(lib) // 1024} KB)")
    return True


if __name__ == "__main__":
    patch_worker_map(os.path.join(FFMPEG,"libavcodec","xb_thread.c"),WORKER_MAP)
    names = sys.argv[1:] or ["pthreads", "avutil", "avcodec"]
    apply_fixups(HOOK_FIXUPS)
    ok = all([build(n) for n in names])
    if ok and 'avcodec' in names:
        with open(os.path.join(FFMPEG,'libavcodec','xb_thread.c'),encoding='utf-8') as f:worker_text=f.read()
        manifest = {'profile': PROFILE,'worker_map':WORKER_MAP,'source_root':FFMPEG,
                    'slice_dispatch':SLICE_DISPATCH,
                    'slice_dispatch_sha256':hashlib.sha256(generate(worker_text).encode('utf-8')).hexdigest() if SLICE_DISPATCH=='batched' else None,
                    'worker_sha256': hashlib.sha256(open(os.path.join(FFMPEG,'libavcodec','xb_thread.c'),'rb').read()).hexdigest(),
                    'libraries': {n: hashlib.sha256(open(os.path.join(OUT,n+'.lib'),'rb').read()).hexdigest()
                                  for n in ('avcodec','avutil','pthreads') if os.path.isfile(os.path.join(OUT,n+'.lib'))}}
        with open(os.path.join(OUT,'build-manifest.json'),'w') as f:
            json.dump(manifest,f,indent=2)
    sys.exit(0 if ok else 1)
