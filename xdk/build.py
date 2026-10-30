"""Builds MoonX360 for the Xbox 360 dashboard (.xex) with the Xbox 360 SDK.

    python xdk/build.py [target ...]     targets: mbedtls opus moonlight app xex (default: all)

Third-party sources live in xdk/deps (fetched separately, see xdk/README.md);
FFmpeg is built by xdk/scripts/build_ffmpeg.py. Objects and libraries go to
xdk/build. Compilation is incremental and parallel.
"""
import concurrent.futures
import glob
import hashlib
import json
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

XDK_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(XDK_DIR)
DEPS = os.path.join(XDK_DIR, "deps")
OUT = os.path.abspath(os.environ.get("XDK_BUILD_DIR", os.path.join(XDK_DIR, "build")))
XEDK = os.environ.get("XEDK", r"C:\AI\xbox\dev kits\XBOX360 SDK 21256.3\extracted\XDK")
JUL12 = os.path.join(XEDK, "TechPreview", "Jul12Compiler")

TOOLS = {name: os.path.join(JUL12, "bin", "win32", name + ".exe") for name in ("cl", "lib", "link")}
TOOLS["imagexex"] = os.path.join(XEDK, "bin", "win32", "imagexex.exe")

ENV = dict(os.environ)
ENV["PATH"] = os.pathsep.join([os.path.join(JUL12, "bin", "win32"), os.path.join(XEDK, "bin", "win32"), ENV["PATH"]])
COMPAT_INC = os.path.join(XDK_DIR, "compat", "include")
# Our compat headers (windows.h, stdbool.h, ...) come before the SDK's.
ENV["INCLUDE"] = os.pathsep.join([COMPAT_INC, os.path.join(JUL12, "include", "xbox"), os.path.join(XEDK, "include", "xbox")])
ENV["LIB"] = os.pathsep.join([os.path.join(JUL12, "lib", "xbox"), os.path.join(XEDK, "lib", "xbox")])

BASE = ["/nologo", "/c", "/O2", "/Oi", "/GF", "/Gy", "/MT", "/GR-", "/W3", "/wd4996",
        "/D", "NDEBUG", "/D", "_XBOX", "/D", "XBOX"]
PROFILE = os.environ.get("XDK_OPTIMIZATION", "aggressive")
if PROFILE not in ("aggressive", "compatible"):
    raise SystemExit("XDK_OPTIMIZATION must be aggressive or compatible")
# Keep mbedTLS and the system compatibility layer on the original settings.
HOT = [f for f in BASE if f != "/O2"] + ["/Ox", "/Ou", "/Oc"] if PROFILE == "aggressive" else BASE

MBEDTLS = os.path.join(DEPS, "mbedtls-2.16.12")
OPUS = os.path.join(DEPS, "opus-1.5.2")
MLC = os.path.join(DEPS, "moonlight-common-c")
FFMPEG = os.path.abspath(os.environ.get('XDK_FFMPEG_ROOT',os.path.join(DEPS, "XBMC-360", "libraries", "ffmpeg")))
WORKER_MAP=os.environ.get('XDK_WORKER_MAP','current')
if WORKER_MAP not in ('current','previous'):raise SystemExit('XDK_WORKER_MAP must be current or previous')
SLICE_DISPATCH=os.environ.get('XDK_SLICE_DISPATCH','legacy')
if SLICE_DISPATCH not in ('legacy','batched'):raise SystemExit('XDK_SLICE_DISPATCH must be legacy or batched')
IDENTITY_DEFS=['/D','XDK_WORKER_MAP_PREVIOUS='+str(int(WORKER_MAP=='previous')),
               '/D','XDK_SLICE_DISPATCH_BATCHED='+str(int(SLICE_DISPATCH=='batched')),
               '/D','XDK_OPTIMIZATION_AGGRESSIVE='+str(int(PROFILE=='aggressive'))]
MBEDTLS_DEFS = ["/I", os.path.join(MBEDTLS, "include"), "/I", XDK_DIR,
                "/D", 'MBEDTLS_USER_CONFIG_FILE="mbedtls_config_xdk.h"']


def run(cmd, cwd=None):
    r = subprocess.run(cmd, env=ENV, capture_output=True, text=True, errors="replace", cwd=cwd)
    return r.returncode, r.stdout + r.stderr


def compile_one(job):
    src, obj, flags = job
    stamp = obj + ".command.json"
    headers = [os.path.join(XDK_DIR, p) for p in
               ("stream_checks.h", "compat/include/xdk_compat.h", "compat/include/winsock2.h",
                "src/xdk.h", "mbedtls_config_xdk.h", "ffmpeg_xbox/xbox_dsp.h")]
    headers += glob.glob(os.path.join(ROOT, "src", "*.h"))
    headers += glob.glob(os.path.join(ROOT, "src", "ui", "*.h"))
    headers += glob.glob(os.path.join(XDK_DIR, "src", "*.h"))
    signature = json.dumps([TOOLS["cl"], flags, ENV["INCLUDE"],
                            [(p, os.path.getmtime(p)) for p in headers]])
    previous = None
    if os.path.exists(stamp):
        with open(stamp) as f:
            previous = f.read()
    if os.path.exists(obj) and previous == signature and os.path.getmtime(obj) >= os.path.getmtime(src):
        return src, 0, ""
    rc, out = run([TOOLS["cl"]] + flags + [src, "/Fo" + obj])
    with open(obj + '.log', 'w', encoding='utf-8') as f:
        f.write(out)
    if rc == 0:
        with open(stamp, "w") as f:
            f.write(signature)
    elif os.path.exists(stamp):
        os.remove(stamp)
    return src, rc, out


def compile_all(name, sources, flags, base=None):
    """Compiles sources into xdk/build/<name>/. A source may be (path, extra_flags).
    Returns the object files, or None if anything failed."""
    obj_dir = os.path.join(OUT, name)
    os.makedirs(obj_dir, exist_ok=True)
    jobs, objs = [], []
    for src in sources:
        extra = []
        if isinstance(src, tuple):
            src, extra = src
        rel = os.path.relpath(src, base or os.path.dirname(src))
        obj = os.path.join(obj_dir, re.sub(r"[\\/:]", "_", os.path.splitext(rel)[0]) + ".obj")
        jobs.append((src, obj, flags + extra))
        objs.append(obj)

    failed = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        for src, rc, out in pool.map(compile_one, jobs):
            if rc != 0:
                failed.append(src)
                errs = [l for l in out.splitlines() if "error" in l.lower()]
                print(f"FAILED {os.path.relpath(src, ROOT)}")
                # XDK_VERBOSE=1 prints every error (e.g. to pipe into scripts/autocast.py).
                print("\n".join("    " + l for l in (errs if os.environ.get("XDK_VERBOSE") else errs[:12])))
    print(f"{name}: {len(jobs)} sources, {len(failed)} failed")
    return None if failed else objs


def build_lib(name, sources, flags, base=None):
    """Compiles sources into xdk/build/<name>/ and archives <name>.lib."""
    objs = compile_all(name, sources, flags, base)
    if objs is None:
        return None
    lib = os.path.join(OUT, name + ".lib")
    rsp = lib + ".rsp"
    with open(rsp, "w") as f:
        f.write("\n".join(f'"{o}"' for o in objs))
    rc, out = run([TOOLS["lib"], "/nologo", "/OUT:" + lib, "@" + rsp])
    if rc != 0:
        print(out)
        return None
    print(f"  -> {os.path.relpath(lib, ROOT)} ({os.path.getsize(lib) // 1024} KB)")
    return lib


# ------------------------------------------------------------------ targets

def target_mbedtls():
    srcs = sorted(glob.glob(os.path.join(MBEDTLS, "library", "*.c")))
    return build_lib("mbedtls", srcs, BASE + ["/TC"] + MBEDTLS_DEFS)


def apply_fixups(base, fixups):
    """Applies exact (file, old, new) text replacements; already-applied ones are skipped.
    Used for the few C99 constructs the SDK's C compiler rejects in third-party code."""
    for rel, old, new in fixups:
        path = os.path.join(base, rel)
        text = open(path, encoding="utf-8", errors="surrogateescape").read()
        if new in text:
            continue
        if old not in text:
            raise SystemExit(f"fixup no longer matches: {rel}")
        with open(path, "w", encoding="utf-8", errors="surrogateescape", newline="") as f:
            f.write(text.replace(old, new, 1))


OPUS_FIXUPS = [
    ("src/repacketizer.c",
     "      opus_int32 frame_ext_count;\n      frame_ext_count = total_ext_count - ext_count;\n"
     "      int ret = opus_packet_extensions_parse(",
     "      opus_int32 frame_ext_count;\n      int ret;\n      frame_ext_count = total_ext_count - ext_count;\n"
     "      ret = opus_packet_extensions_parse("),
]


def opus_sources():
    srcs = []
    for mk, vars_ in (("celt_sources.mk", ["CELT_SOURCES"]),
                      ("silk_sources.mk", ["SILK_SOURCES", "SILK_SOURCES_FLOAT"]),
                      ("opus_sources.mk", ["OPUS_SOURCES", "OPUS_SOURCES_FLOAT"])):
        text = open(os.path.join(OPUS, mk)).read().replace("\\\n", " ")
        for var in vars_:
            m = re.search(rf"^{var}\s*=\s*(.*)$", text, re.M)
            srcs += [os.path.join(OPUS, s) for s in m.group(1).split()]
    # The console only decodes; the encoder also uses C99 the compiler rejects.
    return [s for s in srcs if os.path.basename(s) != "opus_encoder.c"]


def target_opus():
    apply_fixups(OPUS, OPUS_FIXUPS)
    flags = HOT + ["/TC", "/D", "OPUS_BUILD", "/D", "USE_ALLOCA", "/D", "HAVE_LRINTF=0",
                    "/I", os.path.join(OPUS, "include"), "/I", os.path.join(OPUS, "celt"),
                    "/I", os.path.join(OPUS, "silk"), "/I", os.path.join(OPUS, "silk", "float")]
    return build_lib("opus", opus_sources(), flags, base=OPUS)


SRC = os.path.join(XDK_DIR, "src")
COMPAT = os.path.join(XDK_DIR, "compat")


def target_compat():
    srcs = [os.path.join(COMPAT, f) for f in ("win_sync.c", "win_net.c", "crt.c")]
    return build_lib("compat", srcs, BASE + ["/TC"])


def target_moonlight():
    """moonlight-common-c + ENet + nanors, compiled as C++: the SDK's C compiler is
    C89-only, while its C++ compiler accepts the C99 these sources use."""
    apply_fixups(MLC, [("src/Platform.c",
        "#if defined(LC_WINDOWS) && !defined(NXDK)\n    setThreadNameWin32(ctx->name);",
        "#if defined(LC_WINDOWS) && !defined(NXDK)\n    xdk_configure_moonlight_thread(ctx->name);\n    setThreadNameWin32(ctx->name);")])
    srcs = [s for s in sorted(glob.glob(os.path.join(MLC, "src", "*.c")))
            if os.path.basename(s) != "PlatformCrypto.c"]
    srcs.append(os.path.join(SRC, "PlatformCrypto_xdk.c"))
    srcs += [os.path.join(MLC, "enet", f + ".c") for f in
             ("callbacks", "compress", "host", "list", "packet", "peer", "protocol")]
    srcs.append(os.path.join(SRC, "enet_xbox.c"))
    srcs += [os.path.join(MLC, "nanors", "rs.c"),
             os.path.join(MLC, "nanors", "deps", "obl", "oblas_common.c"),
             os.path.join(MLC, "nanors", "deps", "obl", "oblas_lite.c")]
    flags = HOT + ["/TP", "/FIxdk_compat.h", "/D", "HAS_SOCKLEN_T",
                    "/I", os.path.join(MLC, "src"), "/I", os.path.join(MLC, "enet", "include"),
                    "/I", os.path.join(MLC, "nanors"), "/I", os.path.join(MLC, "nanors", "deps"),
                    "/I", os.path.join(MLC, "nanors", "deps", "obl")] + MBEDTLS_DEFS
    return build_lib("moonlight", srcs, flags, base=ROOT)


def generate_fonts():
    """The UI's TTF files as C arrays (the libxenon build uses .incbin instead)."""
    out = os.path.join(OUT, "gen", "fonts_data.c")
    fonts = [("font_regular_ttf", "Inter-Regular.ttf"), ("font_bold_ttf", "Inter-SemiBold.ttf")]
    paths = [os.path.join(ROOT, "assets", "fonts", f) for _, f in fonts]
    if os.path.exists(out) and all(os.path.getmtime(out) >= os.path.getmtime(p) for p in paths):
        return out
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write("/* Generated by xdk/build.py from assets/fonts (SIL Open Font License). */\n")
        for (sym, _), path in zip(fonts, paths):
            data = open(path, "rb").read()
            f.write(f"const unsigned char {sym}[{len(data)}] = {{\n")
            for i in range(0, len(data), 24):
                f.write(",".join(str(b) for b in data[i:i + 24]) + ",\n")
            f.write("};\n")
    return out


APP_OBJS = []


def target_app():
    """Our code: the shared app, GameStream client and UI (src/), compiled as
    C++ like moonlight-common-c, plus the SDK platform layer (xdk/src)."""
    shared = [os.path.join(ROOT, "src", f) for f in
              ("app.c", "config.c", "crypto.c", "http.c", "gamestream.c", "mdns.c", "mdns_net.c", "host_cache.c", "wake_net.c",
               "ui/canvas.c", "ui/font.c", "ui/draw.c", "ui/ui.c", "ui/image.c")]
    platform = [os.path.join(SRC, f + ".cpp") for f in ("main_xdk", "gfx_xdk", "gfx_ui", "audio_xdk", "input_xdk", "video_cached", "gfx_stream_menu", "performance_xdk")]
    flags = HOT + IDENTITY_DEFS + ["/FIxdk_compat.h", "/I", os.path.join(MLC, "src"), "/I", os.path.join(OPUS, "include")] + MBEDTLS_DEFS
    srcs = [(s, ["/TP"]) for s in shared + platform]
    # Only the video module sees FFmpeg's headers: FFmpeg's root has its own config.h.
    srcs.append((os.path.join(SRC, "video_xdk.cpp"), ["/TP", "/I", FFMPEG]))
    srcs.append((generate_fonts(), ["/TC"]))
    objs = compile_all("app", srcs, flags, base=ROOT)
    if objs is None:
        return None
    APP_OBJS[:] = objs
    return objs


SDK_LIBS = ["xapilib.lib", "d3d9.lib", "d3dx9.lib", "xgraphics.lib", "xboxkrnl.lib", "xnet.lib",
            "xaudio2.lib", "x3daudio.lib", "xmcore.lib", "xinput2.lib"]


def target_xex():
    manifest_path = os.path.join(OUT, 'ffmpeg', 'build-manifest.json')
    if not os.path.isfile(manifest_path):
        raise SystemExit('missing verified FFmpeg build: ' + manifest_path)
    with open(manifest_path) as f:
        ffmpeg_build = json.load(f)
    with open(os.path.join(FFMPEG,'libavcodec','xb_thread.c'),'rb') as f:
        worker_hash = hashlib.sha256(f.read()).hexdigest()
    if (ffmpeg_build.get('profile') != PROFILE or ffmpeg_build.get('worker_sha256') != worker_hash
            or ffmpeg_build.get('worker_map','current') != WORKER_MAP):
        raise SystemExit('FFmpeg profile/worker patch changed; rebuild with xdk/scripts/build_ffmpeg.py')
    if ffmpeg_build.get('slice_dispatch','legacy')!=SLICE_DISPATCH:
        raise SystemExit('FFmpeg slice dispatch changed; rebuild with xdk/scripts/build_ffmpeg.py')
    if SLICE_DISPATCH=='batched':
        sys.path.insert(0,os.path.join(XDK_DIR,'scripts'))
        from slice_dispatch import generate
        with open(os.path.join(FFMPEG,'libavcodec','xb_thread.c'),encoding='utf-8') as f:
            generated_hash=hashlib.sha256(generate(f.read()).encode('utf-8')).hexdigest()
        if ffmpeg_build.get('slice_dispatch_sha256')!=generated_hash:
            raise SystemExit('FFmpeg generated slice dispatcher changed; rebuild FFmpeg')
    for name in ('avcodec','avutil','pthreads'):
        path = os.path.join(OUT,'ffmpeg',name+'.lib')
        if not os.path.isfile(path):raise SystemExit('unverified FFmpeg library: ' + path)
        with open(path,'rb') as f:library_hash=hashlib.sha256(f.read()).hexdigest()
        if ffmpeg_build.get('libraries',{}).get(name) != library_hash:
            raise SystemExit('unverified FFmpeg library: ' + path)
    objs = APP_OBJS or target_app()
    if objs is None:
        return None
    libs = [os.path.join(OUT, n + ".lib") for n in ("moonlight", "mbedtls", "opus", "compat")]
    libs += [os.path.join(OUT, "ffmpeg", n + ".lib") for n in ("avcodec", "avutil", "pthreads")]
    for lib in libs:
        if not os.path.exists(lib):
            raise SystemExit(f"missing {os.path.relpath(lib, ROOT)}: build its target first")
    exe = os.path.join(OUT, "MoonX360.exe")
    xex = os.path.join(OUT, "default.xex")
    rsp = exe + ".rsp"
    with open(rsp, "w") as f:
        f.write("\n".join(f'"{o}"' for o in objs + libs + SDK_LIBS))
    # /STACK also sets the default for threads created with stack size 0,
    # which is what moonlight-common-c does.
    # The XDK linker otherwise auto-converts to XEX before our SPA/config exists.
    rc, out = run([TOOLS["link"], "/nologo", "/XEX:NO", "/OUT:" + exe, "/OPT:REF", "/OPT:ICF", "/STACK:524288",
                   "/MAP:" + os.path.join(OUT, "MoonX360.map"),
                   "@" + rsp])
    if rc != 0:
        print(out)
        return None
    # Compile the dashboard name/icon with the installed SDK, and embed its SPA.
    # Keep paths in the generated config absolute so builds work from any cwd.
    spa = os.path.join(OUT, "MoonX360.spa")
    rc, out = run([os.path.join(XEDK, "bin", "win32", "spac.exe"), "-nologo", "-noh",
                   "-o", spa, os.path.join(XDK_DIR, "MoonX360.xlast")])
    if rc != 0 or "error" in out.lower() or not os.path.isfile(spa):
        print(out)
        return None
    config = ET.parse(os.path.join(XDK_DIR, "xex.xml"))
    ET.SubElement(config.getroot(), "section", name="4D583336", file=spa, memory="RO")
    config_path = os.path.join(OUT, "MoonX360.xex.xml")
    config.write(config_path, encoding="utf-8", xml_declaration=True)
    rc, out = run([TOOLS["imagexex"], "/nologo", "/config:" + config_path,
                   "/out:" + xex, exe])
    if rc != 0:
        print(out)
        return None
    extracted = os.path.join(OUT, "MoonX360-embedded.spa")
    rc, out = run([TOOLS["imagexex"], "/dump", "/sectionname:4D583336",
                   "/sectionfile:" + extracted, xex])
    if rc != 0 or not os.path.isfile(extracted):
        print(out)
        return None
    with open(spa, "rb") as expected, open(extracted, "rb") as actual:
        if expected.read() != actual.read():
            raise SystemExit("MoonX360 dashboard SPA did not survive XEX embedding")
    print(f"  -> {os.path.relpath(xex, ROOT)} ({os.path.getsize(xex) // 1024} KB)")
    return xex


TARGETS = {
    "mbedtls": target_mbedtls,
    "opus": target_opus,
    "compat": target_compat,
    "moonlight": target_moonlight,
    "app": target_app,
    "xex": target_xex,
}

if __name__ == "__main__":
    names = sys.argv[1:] or list(TARGETS)
    for n in names:
        if TARGETS[n]() is None:
            sys.exit(1)
