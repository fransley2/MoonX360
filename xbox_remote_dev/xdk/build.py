"""Build isolated Xbox Remote Dev startup artifacts with Xbox 360 XDK.

Outputs go only into xbox_remote_dev/build, never xdk/build/default.xex.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent
OUT = ROOT.parent / "build"
SDK = Path(os.environ.get("XEDK", r"C:\Program Files (x86)\Microsoft Xbox 360 SDK"))
JUL = SDK / "TechPreview" / "Jul12Compiler"


def run(args, env):
    result = subprocess.run([str(arg) for arg in args], env=env, cwd=ROOT,
                            text=True, capture_output=True, errors="replace")
    if result.returncode:
        raise RuntimeError(" ".join(str(arg) for arg in args) + "\n" +
                           result.stdout + result.stderr)
    if result.stdout.strip() or result.stderr.strip():
        print((result.stdout + result.stderr).strip())
    return result.stdout + result.stderr


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join((str(JUL / "bin" / "win32"),
                                   str(SDK / "bin" / "win32"), env.get("PATH", "")))
    env["INCLUDE"] = os.pathsep.join((str(JUL / "include" / "xbox"),
                                      str(SDK / "include" / "xbox")))
    env["LIB"] = os.pathsep.join((str(JUL / "lib" / "xbox"),
                                  str(SDK / "lib" / "xbox")))
    warnings = {}
    for name in ("RemoteAgent", "RemoteTest"):
        source = ROOT / (name + ".cpp")
        obj, exe, xex = (OUT / (name + ext) for ext in (".obj", ".exe", ".xex"))
        run([JUL / "bin" / "win32" / "cl.exe", "/nologo", "/c", "/MT",
             "/O2", "/W3", "/D", "_XBOX", "/D", "XBOX", source,
             "/Fo" + str(obj)], env)
        link = [JUL / "bin" / "win32" / "link.exe", "/nologo", "/XEX:NO",
                "/OUT:" + str(exe), obj, "xapilib.lib", "xboxkrnl.lib"]
        if name == "RemoteAgent":
            link.insert(2, "/DLL")
        run(link, env)
        config = ROOT / ("agent.xex.xml" if name == "RemoteAgent" else "test.xex.xml")
        image_log = run([SDK / "bin" / "win32" / "imagexex.exe", "/nologo",
                         "/config:" + str(config), "/out:" + str(xex), exe], env)
        warnings[name] = [line for line in image_log.splitlines() if "warning" in line.lower()]
        if xex.read_bytes()[:4] != b"XEX2":
            raise RuntimeError(f"invalid XEX2 output: {xex}")
    commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                            capture_output=True, text=True, check=True).stdout.strip()
    manifest = {"source_commit": commit, "hardware_tested": False,
                "plugin_loadability_unverified": True, "tool_warnings": warnings,
                "files": {name + ".xex": {"bytes": (OUT / (name + ".xex")).stat().st_size,
                      "sha256": hashlib.sha256((OUT / (name + ".xex")).read_bytes()).hexdigest()}
                          for name in ("RemoteAgent", "RemoteTest")}}
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
