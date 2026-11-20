"""Build MoonX360 with batch16 FFmpeg and low-latency input."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

XDK = Path(__file__).resolve().parents[1]
ROOT = XDK.parent

BATCH16_PATCHES = (
    "r47c-ordered-deblock",
    "r47c-wavefront-deblock",
    "r47c-row-progress",
    "r47d-batch8",
    "r47d-long-slices",
    "r47d-edge16",
    "r47d-edge-transpose",
    "r47d-dispatch-profile",
    "r47d-first-idr",
    "r48-batch16",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out",
        type=Path,
        default=XDK / "build_best_performance",
        help="New output directory for this build",
    )
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)

    source = out / "ffmpeg-src"
    shutil.copytree(XDK / "deps/XBMC-360/libraries/ffmpeg", source)
    for name in ("h264.h", "xb_thread.c"):
        unit = source / "libavcodec" / name
        old = b"../../../../../ffmpeg_xbox/h264_parallel_diagnostic.h"
        new = os.path.relpath(
            XDK / "ffmpeg_xbox/h264_parallel_diagnostic.h", unit.parent
        ).replace(os.sep, "/").encode()
        original = unit.read_bytes()
        if original.count(old) != 1:
            raise RuntimeError(f"Unexpected diagnostic include in {name}")
        unit.write_bytes(original.replace(old, new))

    patches = [XDK / "patches" / f"{name}.patch" for name in BATCH16_PATCHES]
    files = sorted({
        line[6:]
        for patch in patches
        for line in patch.read_text(encoding="utf-8").splitlines()
        if line.startswith("--- a/libavcodec/")
    })
    patch_work = out / "patch-work"
    (patch_work / "libavcodec").mkdir(parents=True)
    for name in files:
        (patch_work / name).write_bytes(
            (source / name).read_text(encoding="utf-8").encode("utf-8")
        )
    subprocess.run(["git", "init", "-q"], cwd=patch_work, check=True)
    for patch in patches:
        print(f"[build_best] Applying {patch.name}...", flush=True)
        subprocess.run(["git", "apply", str(patch)], cwd=patch_work, check=True)
    for name in files:
        (source / name).write_bytes((patch_work / name).read_bytes())

    env = dict(
        os.environ,
        XDK_BUILD_DIR=str(out),
        XDK_FFMPEG_ROOT=str(source),
        XDK_CONFIGURATION="Release",
        XDK_OPTIMIZATION="aggressive",
        XDK_LOW_LATENCY_INPUT="1",
        XDK_WORKER_MAP="current",
        XDK_SLICE_DISPATCH="legacy",
        XDK_VMX_AB_EXPERIMENT="0",
    )
    if "XEDK" not in env:
        default_xedk = r"C:\Program Files (x86)\Microsoft Xbox 360 SDK"
        if os.path.isdir(default_xedk):
            env["XEDK"] = default_xedk

    for script in ("xdk/scripts/build_ffmpeg.py", "xdk/build.py"):
        print(f"[build_best] Running {script}...")
        subprocess.run([sys.executable, script], cwd=ROOT, env=env, check=True)

    xex = out / "default.xex"
    print(f"[build_best] Build succeeded: {xex} ({xex.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
