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
    "v026-opt1-edge-nolhs",
    "v026-opt4-chroma-lf-nolhs",
    "v026-chroma-optA-vmx1d",
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
    patches = [XDK / "patches" / f"{name}.patch" for name in BATCH16_PATCHES]
    libavcodec_files = []
    xbox_files = []
    for patch in patches:
        for line in patch.read_text(encoding="utf-8").splitlines():
            if line.startswith("--- a/libavcodec/"):
                rel = line[len("--- a/"):].strip()
                if rel not in libavcodec_files:
                    libavcodec_files.append(rel)
            elif line.startswith("--- a/ffmpeg_xbox/"):
                rel = line[len("--- a/"):].strip()
                if rel not in xbox_files:
                    xbox_files.append(rel)

    xbox_dir = None
    if xbox_files:
        xbox_dir = out / "ffmpeg-xbox"
        shutil.copytree(XDK / "ffmpeg_xbox", xbox_dir)

    diag_hdr = (xbox_dir if xbox_dir is not None else (XDK / "ffmpeg_xbox")) / "h264_parallel_diagnostic.h"
    rel_hdr = os.path.relpath(diag_hdr, source / "libavcodec").replace(os.sep, "/").encode()
    for name in ("h264.h", "xb_thread.c"):
        unit = source / "libavcodec" / name
        old = b"../../../../../ffmpeg_xbox/h264_parallel_diagnostic.h"
        original = unit.read_bytes()
        if original.count(old) != 1:
            raise RuntimeError(f"Unexpected diagnostic include in {name}")
        unit.write_bytes(original.replace(old, rel_hdr))

    patch_work = out / "patch-work"
    (patch_work / "libavcodec").mkdir(parents=True)
    if xbox_files:
        (patch_work / "ffmpeg_xbox").mkdir(parents=True)
    for name in libavcodec_files:
        (patch_work / name).write_bytes(
            (source / name).read_text(encoding="utf-8").encode("utf-8")
        )
    for name in xbox_files:
        assert xbox_dir is not None
        (patch_work / name).write_bytes(
            (xbox_dir / name[len("ffmpeg_xbox/"):]).read_text(encoding="utf-8").encode("utf-8")
        )
    subprocess.run(["git", "init", "-q"], cwd=patch_work, check=True)
    for patch in patches:
        print(f"[build_best] Applying {patch.name}...", flush=True)
        subprocess.run(["git", "apply", str(patch)], cwd=patch_work, check=True)
    for name in libavcodec_files:
        (source / name).write_bytes((patch_work / name).read_bytes())
    for name in xbox_files:
        assert xbox_dir is not None
        (xbox_dir / name[len("ffmpeg_xbox/"):]).write_bytes((patch_work / name).read_bytes())

    env = dict(
        os.environ,
        XDK_BUILD_DIR=str(out),
        XDK_FFMPEG_ROOT=str(source),
        XDK_CONFIGURATION="Release",
        XDK_OPTIMIZATION="aggressive",
        XDK_LOW_LATENCY_INPUT="1",
        XDK_INPUT_HW_THREAD="0",
        XDK_WORKER_MAP="current",
        XDK_SLICE_DISPATCH="legacy",
        XDK_VMX_AB_EXPERIMENT="0",
        XDK_H264_REFERENCE_MODE="0",
        XDK_OPTIMIZED_YUV_COPY="0",
        XDK_YUV_STREAMING_COPY="0",
        XDK_YUV_CONTIGUOUS_PLANES="1",
        XDK_INPUT_FAST_KB_SKIP="0",
        XDK_WIRED_1392_MIN_KBPS="4000",
    )
    if xbox_dir is not None:
        env["XDK_FFMPEG_XBOX_DIR"] = str(xbox_dir)
    if "XEDK" not in env:
        default_xedk = r"C:\Program Files (x86)\Microsoft Xbox 360 SDK"
        if os.path.isdir(default_xedk):
            env["XEDK"] = default_xedk

    for script in ("xdk/scripts/build_ffmpeg.py", "xdk/build.py"):
        print(f"[build_best] Running {script}...")
        subprocess.run([sys.executable, script], cwd=ROOT, env=env, check=True)

    launcher_xex = out / "default.xex"
    app_xex = out / "moonx360.xex"
    print(f"[build_best] Build succeeded: launcher={launcher_xex} ({launcher_xex.stat().st_size} bytes), app={app_xex} ({app_xex.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
