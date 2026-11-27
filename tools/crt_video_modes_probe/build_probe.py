"""Builds the standalone CRT Video Modes Probe XEX into tools/crt_video_modes_probe/build/."""
import os
import subprocess
import sys
import xml.etree.ElementTree as ET

TOOL_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(TOOL_DIR))
XDK_DIR = os.path.join(ROOT, "xdk")
OUT = os.path.join(TOOL_DIR, "build")
os.makedirs(OUT, exist_ok=True)

XEDK = os.environ.get("XEDK", r"C:\Program Files (x86)\Microsoft Xbox 360 SDK")
JUL12 = os.path.join(XEDK, "TechPreview", "Jul12Compiler")

TOOLS = {name: os.path.join(JUL12, "bin", "win32", name + ".exe") for name in ("cl", "link")}
TOOLS["imagexex"] = os.path.join(XEDK, "bin", "win32", "imagexex.exe")

ENV = dict(os.environ)
ENV["PATH"] = os.pathsep.join([os.path.join(JUL12, "bin", "win32"), os.path.join(XEDK, "bin", "win32"), ENV["PATH"]])
ENV["INCLUDE"] = os.pathsep.join([os.path.join(JUL12, "include", "xbox"), os.path.join(XEDK, "include", "xbox")])
ENV["LIB"] = os.pathsep.join([os.path.join(JUL12, "lib", "xbox"), os.path.join(XEDK, "lib", "xbox")])


def run(cmd):
    r = subprocess.run(cmd, env=ENV, capture_output=True, text=True, errors="replace", cwd=OUT)
    if r.returncode != 0:
        print(r.stdout + r.stderr)
        raise SystemExit(r.returncode)
    return r.stdout + r.stderr


def main():
    src = os.path.join(TOOL_DIR, "main.cpp")
    obj = os.path.join(OUT, "main.obj")
    exe = os.path.join(OUT, "crt_video_modes_probe.exe")
    xex = os.path.join(OUT, "default.xex")

    run([
        TOOLS["cl"], "/nologo", "/c", "/O2", "/Oi", "/MT", "/GR-", "/W3", "/wd4996",
        "/D", "_XBOX", "/D", "XBOX", "/D", "NDEBUG", "/TP",
        src, "/Fo" + obj
    ])
    run([
        TOOLS["link"], "/nologo", "/XEX:NO", "/OUT:" + exe, "/OPT:REF", "/OPT:ICF",
        "/STACK:262144", "/MAP:crt_video_modes_probe.map",
        obj, "xapilib.lib", "d3d9.lib", "d3dx9.lib", "xgraphics.lib", "xboxkrnl.lib", "xinput2.lib"
    ])
    xml_path = os.path.join(XDK_DIR, "xex.xml")
    run([TOOLS["imagexex"], "/nologo", "/config:" + xml_path, "/out:" + xex, exe])
    print(f"OK -> {xex} ({os.path.getsize(xex)} bytes)")


if __name__ == "__main__":
    main()
