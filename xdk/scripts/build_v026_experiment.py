"""Build an isolated MoonX360 v0.2.6 diagnostic or A/B experiment artifact."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import time


SCRIPT_DIR = Path(__file__).resolve().parent
XDK = SCRIPT_DIR.parent
ROOT = XDK.parent
BASELINE_XEX = XDK / "build_best_performance" / "default.xex"
BASELINE_SHA256 = "527e137ec432f6978422e12c0e1529d139f0b6594fec9398ae52d22db62fe201"
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
UNIFIED_STAGE_PATCH = "v026-unified-stage-profile"
V026_VALIDATED_LF_PATCHES = (
    "v026-opt1-edge-nolhs",
    "v026-opt4-chroma-lf-nolhs",
)
V026_RC2_PATCHES = (
    "v026-opt1-edge-nolhs",
    "v026-opt4-chroma-lf-nolhs",
    "v026-chroma-optA-vmx1d",
)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def verify_baseline_intact() -> str:
    if not BASELINE_XEX.is_file():
        raise SystemExit(f"Baseline XEX missing: {BASELINE_XEX}")
    actual = sha256_file(BASELINE_XEX)
    if actual.lower() != BASELINE_SHA256.lower():
        raise SystemExit(
            f"Baseline XEX SHA-256 mismatch! expected={BASELINE_SHA256} actual={actual}"
        )
    return actual


def _on_rmtree_error(func, path, exc_info):
    try:
        os.chmod(path, stat.S_IWRITE | stat.S_IREAD)
        func(path)
    except OSError:
        time.sleep(0.1)
        func(path)


def rmtree_force(path: Path) -> None:
    if not path.exists():
        return
    for attempt in range(4):
        try:
            shutil.rmtree(path, onerror=_on_rmtree_error)
            return
        except OSError:
            if attempt == 3:
                raise
            time.sleep(0.25)


def ensure_safe_output(out: Path, overwrite: bool) -> None:
    resolved = out.resolve()
    forbidden = (
        XDK / "build_best_performance",
        XDK / "deps" / "XBMC-360",
    )
    for guard in forbidden:
        guard_resolved = guard.resolve()
        if resolved == guard_resolved or guard_resolved in resolved.parents or resolved in guard_resolved.parents:
            raise SystemExit(f"Refusing unsafe output path inside {guard_resolved}: {resolved}")
    if resolved.exists():
        if not overwrite:
            raise SystemExit(f"Output directory already exists (pass --overwrite or pick a fresh dir): {resolved}")
        rmtree_force(resolved)


def prepare_ffmpeg_source(out: Path, patches: list[str]) -> tuple[list[dict], Path | None]:
    source = out / "ffmpeg-src"
    shutil.copytree(XDK / "deps" / "XBMC-360" / "libraries" / "ffmpeg", source)
    patch_paths = [XDK / "patches" / f"{name}.patch" for name in patches]
    patch_meta = []
    libavcodec_targets: list[str] = []
    xbox_targets: list[str] = []
    for name, patch_path in zip(patches, patch_paths):
        if not patch_path.is_file():
            raise SystemExit(f"Missing patch file: {patch_path}")
        patch_meta.append({
            "name": name,
            "path": os.path.relpath(patch_path, ROOT).replace("\\", "/"),
            "sha256": sha256_file(patch_path),
        })
        for line in patch_path.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("--- a/libavcodec/"):
                rel = line[len("--- a/"):].strip()
                if rel not in libavcodec_targets:
                    libavcodec_targets.append(rel)
            elif line.startswith("--- a/ffmpeg_xbox/"):
                rel = line[len("--- a/"):].strip()
                if rel not in xbox_targets:
                    xbox_targets.append(rel)

    xbox_dir: Path | None = None
    if xbox_targets:
        xbox_dir = out / "ffmpeg-xbox"
        shutil.copytree(XDK / "ffmpeg_xbox", xbox_dir)

    diag_hdr = (xbox_dir if xbox_dir is not None else (XDK / "ffmpeg_xbox")) / "h264_parallel_diagnostic.h"
    rel_hdr = os.path.relpath(diag_hdr, source / "libavcodec").replace("\\", "/").encode("ascii")
    old_hdr = b"../../../../../ffmpeg_xbox/h264_parallel_diagnostic.h"
    for unit in (source / "libavcodec" / "h264.h", source / "libavcodec" / "xb_thread.c"):
        text = unit.read_bytes()
        if old_hdr not in text:
            raise SystemExit(f"Expected diagnostic header reference not found in {unit}")
        unit.write_bytes(text.replace(old_hdr, rel_hdr))

    patch_work = out / "patch-work"
    if libavcodec_targets:
        (patch_work / "libavcodec").mkdir(parents=True, exist_ok=True)
    if xbox_targets:
        (patch_work / "ffmpeg_xbox").mkdir(parents=True, exist_ok=True)
    for rel in libavcodec_targets:
        shutil.copy2(source / rel, patch_work / rel)
    for rel in xbox_targets:
        assert xbox_dir is not None
        shutil.copy2(xbox_dir / rel[len("ffmpeg_xbox/"):], patch_work / rel)
    if libavcodec_targets or xbox_targets:
        subprocess.run(["git", "init", "-q"], cwd=patch_work, check=True)
        for patch_path in patch_paths:
            subprocess.run(["git", "apply", str(patch_path)], cwd=patch_work, check=True)
        for rel in libavcodec_targets:
            shutil.copy2(patch_work / rel, source / rel)
        for rel in xbox_targets:
            assert xbox_dir is not None
            shutil.copy2(patch_work / rel, xbox_dir / rel[len("ffmpeg_xbox/"):])
    rmtree_force(patch_work)
    return patch_meta, xbox_dir


def parse_imagexex_dump(dump_text: str) -> dict:
    title_id = None
    pe_name = None
    imports: list[str] = []
    embedded_spa = False
    in_imports = False
    in_resource = False
    for raw_line in dump_text.splitlines():
        line = raw_line.strip()
        m_title = re.match(r"^([0-9A-Fa-f]{8})\s+title ID$", line)
        if m_title:
            title_id = m_title.group(1).upper()
        if line.startswith("Original PE image name:"):
            pe_name = line.split(":", 1)[1].strip()
        if line == "SYSTEM IMPORT LIBRARIES":
            in_imports = True
            in_resource = False
            continue
        if line.startswith("RESOURCE SECTION #"):
            in_imports = False
            in_resource = True
            continue
        if in_imports:
            if not line:
                in_imports = False
            else:
                imports.append(line.split()[0])
        elif in_resource:
            if not line:
                in_resource = False
            elif title_id and re.match(rf"^{title_id}\s+name$", line, re.IGNORECASE):
                embedded_spa = True
    return {
        "title_id": title_id,
        "pe_name": pe_name,
        "system_imports": imports,
        "has_xbdm": any(imp.lower() == "xbdm.xex" for imp in imports),
        "embedded_spa_resource": embedded_spa,
    }


def run_imagexex_dump(xedk: str, xex_path: Path, dump_out: Path) -> dict:
    imagexex = Path(xedk) / "bin" / "win32" / "imagexex.exe"
    proc = subprocess.run(
        [str(imagexex), "/dump", str(xex_path)],
        capture_output=True,
        text=True,
        check=True,
    )
    dump_out.write_text(proc.stdout, encoding="utf-8")
    return parse_imagexex_dump(proc.stdout)


def compute_patch_meta(patches: list[str]) -> list[dict]:
    patch_meta = []
    for name in patches:
        patch_path = XDK / "patches" / f"{name}.patch"
        if not patch_path.is_file():
            raise SystemExit(f"Missing patch file: {patch_path}")
        patch_meta.append({
            "name": name,
            "path": os.path.relpath(patch_path, ROOT).replace("\\", "/"),
            "sha256": sha256_file(patch_path),
        })
    return patch_meta


def reuse_ffmpeg_build(src_dir: Path, out: Path, patches: list[str], flags: dict[str, str]) -> tuple[list[dict], str]:
    src = src_dir.resolve()
    if src == out.resolve():
        raise SystemExit(f"--reuse-ffmpeg-from cannot point to output dir itself: {src}")
    manifest_file = src / "experiment-manifest.json"
    ffmpeg_manifest_file = src / "ffmpeg" / "build-manifest.json"
    src_ffmpeg = src / "ffmpeg"
    src_ffmpeg_src = src / "ffmpeg-src"
    for req in (manifest_file, ffmpeg_manifest_file, src_ffmpeg, src_ffmpeg_src):
        if not req.exists():
            raise SystemExit(f"Cannot reuse FFmpeg from {src}; missing {req}")

    src_manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
    expected_patches = compute_patch_meta(patches)
    if src_manifest.get("patches") != expected_patches:
        raise SystemExit(f"Patch mismatch in --reuse-ffmpeg-from {src}")
    ffmpeg_flags = (
        "XDK_CONFIGURATION",
        "XDK_OPTIMIZATION",
        "XDK_WORKER_MAP",
        "XDK_SLICE_DISPATCH",
        "XDK_H264_STAGE_PROFILE",
        "XDK_VMX_AB_EXPERIMENT",
    )
    src_flags = src_manifest.get("flags", {})
    for key in ffmpeg_flags:
        if src_flags.get(key) != flags[key]:
            raise SystemExit(
                f"Flag {key} mismatch in --reuse-ffmpeg-from {src}: "
                f"{src_flags.get(key)!r} != {flags[key]!r}"
            )

    shutil.copytree(src_ffmpeg_src, out / "ffmpeg-src")
    shutil.copytree(src_ffmpeg, out / "ffmpeg")
    if (src / "ffmpeg-xbox").is_dir():
        shutil.copytree(src / "ffmpeg-xbox", out / "ffmpeg-xbox")
    reused_rel = os.path.relpath(src, ROOT).replace("\\", "/")
    return expected_patches, reused_rel


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True, help="Experiment identifier, e.g. v026-stage0-profiler")
    parser.add_argument("--out", type=Path, required=True, help="Isolated output directory")
    parser.add_argument("--preset", choices=("none", "v026-lf", "v026-rc2"), default="none", help="Optional validated patch preset")
    parser.add_argument("--stage-profile", action="store_true", help="Enable unified Stage 0 profiler patch and macros")
    parser.add_argument("--extra-patch", action="append", default=[], help="Optional extra patch name(s) under xdk/patches/ applied after Stage 0 patch")
    parser.add_argument("--input-hw", choices=("0", "3", "5"), default="5", help="Xenon HW thread affinity for LOW_LATENCY_INPUT=1")
    parser.add_argument("--slice-dispatch", choices=("legacy", "batched"), default="legacy", help="FFmpeg slice scheduler mode")
    parser.add_argument("--optimized-yuv-copy", action="store_true", help="Enable XDK_OPTIMIZED_YUV_COPY=1")
    parser.add_argument("--yuv-streaming-copy", action="store_true", help="Enable XDK_YUV_STREAMING_COPY=1")
    parser.add_argument("--yuv-contiguous-planes", action="store_true", help="Enable XDK_YUV_CONTIGUOUS_PLANES=1")
    parser.add_argument("--input-fast-kb-skip", action="store_true", help="Enable XDK_INPUT_FAST_KB_SKIP=1")
    parser.add_argument("--wired-1392-min-kbps", default="12000", help="Threshold for wired 1392B UDP packets")
    parser.add_argument("--enable-auto-live-test", action="store_true", help="Enable isolated auto-live-test.once diagnostic hook (off for public builds)")
    parser.add_argument("--reuse-ffmpeg-from", type=Path, default=None, help="Reuse prebuilt ffmpeg/ and ffmpeg-src/ from an existing experiment dir")
    parser.add_argument("--overwrite", action="store_true", help="Replace existing --out directory if present")
    args = parser.parse_args()

    out = args.out if args.out.is_absolute() else (ROOT / args.out)
    reuse_src = None
    if args.reuse_ffmpeg_from is not None:
        reuse_src = args.reuse_ffmpeg_from if args.reuse_ffmpeg_from.is_absolute() else (ROOT / args.reuse_ffmpeg_from)
        if reuse_src.resolve() == out.resolve():
            raise SystemExit(f"--reuse-ffmpeg-from cannot equal --out: {out}")

    baseline_before = verify_baseline_intact()
    patches = list(BATCH16_PATCHES)
    if args.preset == "v026-lf":
        for name in V026_VALIDATED_LF_PATCHES:
            if name not in patches:
                patches.append(name)
    elif args.preset == "v026-rc2":
        for name in V026_RC2_PATCHES:
            if name not in patches:
                patches.append(name)
    if args.stage_profile:
        patches.append(UNIFIED_STAGE_PATCH)
    for extra in args.extra_patch:
        name = extra[:-6] if extra.endswith(".patch") else extra
        if name not in patches:
            patches.append(name)

    xedk = os.environ.get("XEDK", r"C:\Program Files (x86)\Microsoft Xbox 360 SDK")
    flags = {
        "XDK_CONFIGURATION": "Release",
        "XDK_OPTIMIZATION": "aggressive",
        "XDK_WORKER_MAP": "current",
        "XDK_SLICE_DISPATCH": args.slice_dispatch,
        "XDK_LOW_LATENCY_INPUT": "1",
        "XDK_INPUT_HW_THREAD": args.input_hw,
        "XDK_ENABLE_AUTO_LIVE_TEST": "1" if args.enable_auto_live_test else "0",
        "XDK_H264_STAGE_PROFILE": "1" if args.stage_profile else "0",
        "XDK_VMX_AB_EXPERIMENT": "0",
        "XDK_H264_PARALLEL_DIAGNOSTIC": "0",
        "XDK_H264_REFERENCE_MODE": "0",
        "XDK_H264_NO_RFI": "0",
        "XDK_H264_SINGLE_SLICE_REQUEST": "0",
        "XDK_OPTIMIZED_YUV_COPY": "1" if args.optimized_yuv_copy else "0",
        "XDK_YUV_STREAMING_COPY": "1" if args.yuv_streaming_copy else "0",
        "XDK_YUV_CONTIGUOUS_PLANES": "1" if args.yuv_contiguous_planes else "0",
        "XDK_INPUT_FAST_KB_SKIP": "1" if args.input_fast_kb_skip else "0",
        "XDK_WIRED_1392_MIN_KBPS": str(args.wired_1392_min_kbps),
        "XDK_DIAGNOSTIC_LEGACY_UPLOAD": "0",
        "XDK_ADAPTIVE_FAST_DECODE": "0",
    }

    ensure_safe_output(out, args.overwrite)
    out.mkdir(parents=True, exist_ok=True)

    reused_ffmpeg_from = None
    xbox_dir: Path | None = None
    if reuse_src is not None:
        patch_meta, reused_ffmpeg_from = reuse_ffmpeg_build(reuse_src, out, patches, flags)
        if (out / "ffmpeg-xbox").is_dir():
            xbox_dir = out / "ffmpeg-xbox"
        scripts_to_run = ("xdk/build.py",)
    else:
        patch_meta, xbox_dir = prepare_ffmpeg_source(out, patches)
        scripts_to_run = ("xdk/scripts/build_ffmpeg.py", "xdk/build.py")

    env = dict(
        os.environ,
        XEDK=xedk,
        XDK_BUILD_DIR=str(out),
        XDK_FFMPEG_ROOT=str(out / "ffmpeg-src"),
        **flags,
    )
    if xbox_dir is not None:
        env["XDK_FFMPEG_XBOX_DIR"] = str(xbox_dir)

    for script in scripts_to_run:
        subprocess.run([sys.executable, script], cwd=ROOT, env=env, check=True)

    baseline_after = verify_baseline_intact()
    xex_path = out / "default.xex"
    if not xex_path.is_file():
        raise SystemExit(f"Build did not produce {xex_path}")

    dump_info = run_imagexex_dump(xedk, xex_path, out / "default.xex.imagexex-dump.txt")
    ffmpeg_manifest = json.loads((out / "ffmpeg" / "build-manifest.json").read_text(encoding="utf-8"))

    tracked_files = (
        "default.xex",
        "MoonX360.exe",
        "MoonX360.map",
        "MoonX360.xdb",
        "MoonX360.spa",
        "MoonX360-embedded.spa",
        "ffmpeg/avcodec.lib",
        "ffmpeg/avutil.lib",
        "ffmpeg/pthreads.lib",
        "ffmpeg/build-manifest.json",
    )
    artifacts = {}
    for rel in tracked_files:
        p = out / rel
        if p.is_file():
            artifacts[rel] = {"sha256": sha256_file(p), "bytes": p.stat().st_size}

    manifest = {
        "schema": 1,
        "name": args.name,
        "created_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "output_dir": os.path.relpath(out, ROOT).replace("\\", "/"),
        "baseline": {
            "path": "xdk/build_best_performance/default.xex",
            "sha256": baseline_after,
            "verified_intact": baseline_before == baseline_after == BASELINE_SHA256,
        },
        "flags": flags,
        "reused_ffmpeg_from": reused_ffmpeg_from,
        "patches": patch_meta,
        "ffmpeg_manifest": ffmpeg_manifest,
        "imagexex_dump": dump_info,
        "artifacts": artifacts,
    }
    manifest_path = out / "experiment-manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    sums_lines = []
    for rel, info in artifacts.items():
        sums_lines.append(f"{info['sha256']} *{rel}")
    sums_lines.append(f"{sha256_file(manifest_path)} *experiment-manifest.json")
    (out / "SHA256SUMS.txt").write_text("\n".join(sums_lines) + "\n", encoding="utf-8")

    print(
        f"[v0.2.6] {args.name} -> {xex_path} "
        f"(sha256={artifacts['default.xex']['sha256']} title_id={dump_info['title_id']} "
        f"stage_profile={flags['XDK_H264_STAGE_PROFILE']} input_hw={flags['XDK_INPUT_HW_THREAD']})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
