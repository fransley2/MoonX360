"""Verification tests for MoonX360 v0.2.6 Stage 0 profiler and InputPoll A/B #1 builds."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


TESTS_DIR = Path(__file__).resolve().parent
XDK = TESTS_DIR.parent
ROOT = XDK.parent
sys.path.insert(0, str(XDK / "scripts"))

import analyze_h264_stage_trace  # noqa: E402
import build_v026_experiment  # noqa: E402


BASELINE_XEX = XDK / "build_best_performance" / "default.xex"
BASELINE_SHA256 = "527e137ec432f6978422e12c0e1529d139f0b6594fec9398ae52d22db62fe201"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def test_baseline_intact() -> None:
    assert BASELINE_XEX.is_file(), f"Missing baseline: {BASELINE_XEX}"
    actual = sha256_file(BASELINE_XEX)
    assert actual == BASELINE_SHA256, f"Baseline SHA-256 drifted: {actual} != {BASELINE_SHA256}"


def test_build_py_env_and_manifest_guards() -> None:
    base_env = dict(
        os.environ,
        XDK_CONFIGURATION="Release",
        XDK_OPTIMIZATION="aggressive",
        XDK_WORKER_MAP="current",
        XDK_SLICE_DISPATCH="legacy",
        XDK_VMX_AB_EXPERIMENT="0",
    )

    bad_hw = subprocess.run(
        [sys.executable, str(XDK / "build.py"), "xex"],
        cwd=ROOT,
        env=dict(base_env, XDK_INPUT_HW_THREAD="4"),
        capture_output=True,
        text=True,
    )
    assert bad_hw.returncode != 0
    assert "XDK_INPUT_HW_THREAD" in (bad_hw.stdout + bad_hw.stderr)

    bad_stage = subprocess.run(
        [sys.executable, str(XDK / "build.py"), "xex"],
        cwd=ROOT,
        env=dict(base_env, XDK_H264_STAGE_PROFILE="2"),
        capture_output=True,
        text=True,
    )
    assert bad_stage.returncode != 0
    assert "XDK_H264_STAGE_PROFILE" in (bad_stage.stdout + bad_stage.stderr)

    ffmpeg_src = XDK / "deps" / "XBMC-360" / "libraries" / "ffmpeg"
    worker_sha = sha256_file(ffmpeg_src / "libavcodec" / "xb_thread.c")
    with tempfile.TemporaryDirectory() as tmp:
        tmp_build = Path(tmp)
        ffmpeg_out = tmp_build / "ffmpeg"
        ffmpeg_out.mkdir(parents=True)
        (ffmpeg_out / "build-manifest.json").write_text(
            json.dumps({
                "profile": "aggressive",
                "worker_map": "current",
                "vmx_ab_experiment": 0,
                "h264_stage_profile": 0,
                "slice_dispatch": "legacy",
                "worker_sha256": worker_sha,
            }),
            encoding="utf-8",
        )
        mismatch = subprocess.run(
            [sys.executable, str(XDK / "build.py"), "xex"],
            cwd=ROOT,
            env=dict(
                base_env,
                XDK_BUILD_DIR=str(tmp_build),
                XDK_FFMPEG_ROOT=str(ffmpeg_src),
                XDK_H264_STAGE_PROFILE="1",
            ),
            capture_output=True,
            text=True,
        )
        assert mismatch.returncode != 0
        combined = mismatch.stdout + mismatch.stderr
        assert "stage profile" in combined.lower(), f"Unexpected mismatch output: {combined}"


def test_stage_trace_analyzer_schema1_and_schema2() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        tdir = Path(tmp)
        # 1) Backward compatibility with schema=1
        s1_stage = tdir / "stage1.txt"
        s1_trace = tdir / "trace1.txt"
        s1_stage.write_text("schema=1\nqpc_frequency=50000000\n", encoding="utf-8")
        s1_trace.write_text(
            "last_frames=2\n"
            "columns=frame,decode_us,slice_ticks,loopfilter_ticks,entropy_sample_ticks,entropy_samples\n"
            "1,5000,150000,50000,1000,2\n"
            "2,6000,200000,60000,2000,4\n",
            encoding="utf-8",
        )
        r1 = analyze_h264_stage_trace.analyze(s1_trace, s1_stage)
        assert r1["schema"] == 1
        assert r1["frames"] == 2
        assert abs(r1["slice_cpu_ms"]["mean"] - 3.5) < 1e-6
        assert abs(r1["slice_nonfilter_cpu_ms"]["mean"] - 2.4) < 1e-6

        # 2) Schema=2 with postponed deblocking (loopfilter_ticks intentionally > slice_ticks)
        s2_stage = tdir / "stage2.txt"
        s2_trace = tdir / "trace2.txt"
        s2_stage.write_text("schema=2\nclock=mftb32_ticks\nqpc_frequency=50000000\n", encoding="utf-8")
        cols2 = (
            "frame,decode_us,idr,slices,idc0,idc1,idc2,filter_off,filter_across,filter_within,"
            "ref_slices,cabac_slices,contexts_requested,contexts_effective,serial_dispatches,"
            "parallel_dispatches,max_dispatch_contexts,worker_participants,concurrent_workers,"
            "forced_serial,slice_ticks,loopfilter_ticks,entropy_sample_ticks,entropy_samples,"
            "reconstruct_wall,filter_wall,mc_ticks,idct_ticks,intra_ticks,lf_cache_ticks,"
            "lf_filter_ticks,wavefront_wait_ticks,dispatch_sync_ticks,worker_span_ticks"
        )
        # slice_ticks=300k (6ms sum), loopfilter_ticks=350k (7ms sum > 6ms slice_ticks because postponed!)
        row_a = "10,6000,0,16,0,0,16,0,0,16,16,16,4,4,0,1,16,4,4,0,300000,350000,80000,3600,100000,90000,90000,50000,30000,120000,200000,15000,5000,95000"
        row_b = "11,5000,0,16,0,0,16,0,0,16,16,16,4,4,0,1,16,4,4,0,250000,260000,70000,3600,80000,70000,75000,45000,25000,100000,150000,10000,4000,75000"
        s2_trace.write_text(f"last_frames=2\ncolumns={cols2}\n{row_a}\n{row_b}\n", encoding="utf-8")
        r2 = analyze_h264_stage_trace.analyze(s2_trace, s2_stage)
        assert r2["schema"] == 2
        assert r2["clock"] == "mftb32_ticks"
        assert "wall_ms" in r2 and "worker_cpu_sum_ms" in r2
        assert abs(r2["wall_ms"]["reconstruct_wall"]["mean"] - 1.8) < 1e-6
        assert abs(r2["wall_ms"]["filter_wall"]["mean"] - 1.6) < 1e-6
        assert abs(r2["worker_cpu_sum_ms"]["mc"]["mean"] - 1.65) < 1e-6
        assert abs(r2["worker_cpu_sum_ms"]["lf_filter"]["mean"] - 3.5) < 1e-6
        assert abs(r2["worker_cpu_sum_ms"]["wavefront_wait"]["mean"] - 0.25) < 1e-6

        # 3) Reject invalid schema=2 breakdown where lf_cache + lf_filter > loopfilter_ticks
        bad_lf = tdir / "trace2_bad_lf.txt"
        bad_row_lf = "10,6000,0,16,0,0,16,0,0,16,16,16,4,4,0,1,16,4,4,0,300000,100000,50000,3600,100000,90000,80000,50000,30000,60000,50000,0,5000,95000"
        bad_lf.write_text(f"last_frames=1\ncolumns={cols2}\n{bad_row_lf}\n", encoding="utf-8")
        try:
            analyze_h264_stage_trace.analyze(bad_lf, s2_stage)
            raise AssertionError("Expected ValueError for lf_cache + lf_filter > loopfilter_ticks")
        except ValueError as exc:
            assert "loopfilter" in str(exc).lower()

        # 4) Reject invalid schema=2 breakdown where recon substages > slice_ticks
        bad_rec = tdir / "trace2_bad_rec.txt"
        bad_row_rec = "10,6000,0,16,0,0,16,0,0,16,16,16,4,4,0,1,16,4,4,0,200000,100000,80000,3600,100000,50000,80000,40000,30000,40000,50000,0,5000,95000"
        bad_rec.write_text(f"last_frames=1\ncolumns={cols2}\n{bad_row_rec}\n", encoding="utf-8")
        try:
            analyze_h264_stage_trace.analyze(bad_rec, s2_stage)
            raise AssertionError("Expected ValueError for recon sub-stages > slice_ticks")
        except ValueError as exc:
            assert "slice ticks" in str(exc).lower()


def verify_sha256sums_file(out_dir: Path) -> None:
    sums_path = out_dir / "SHA256SUMS.txt"
    assert sums_path.is_file(), f"Missing SHA256SUMS.txt in {out_dir}"
    lines = [line.strip() for line in sums_path.read_text(encoding="utf-8").splitlines() if line.strip()]
    assert lines, f"Empty SHA256SUMS.txt in {out_dir}"
    for line in lines:
        digest, rel = line.split(" *", 1)
        target = out_dir / rel
        assert target.is_file(), f"Missing checksummed file {target}"
        actual = sha256_file(target)
        assert actual == digest, f"SHA-256 mismatch for {target}: {actual} != {digest}"


def verify_experiment_build(name: str, expected_stage_profile: int, expected_input_hw: str, expected_patches: list[str]) -> dict:
    out_dir = XDK / "build" / name
    xex_path = out_dir / "default.xex"
    manifest_path = out_dir / "experiment-manifest.json"
    dump_path = out_dir / "default.xex.imagexex-dump.txt"

    assert xex_path.is_file(), f"Missing built XEX: {xex_path}"
    assert xex_path.read_bytes()[:4] == b"XEX2", f"Invalid XEX2 magic in {xex_path}"
    assert manifest_path.is_file(), f"Missing experiment-manifest.json in {out_dir}"
    assert dump_path.is_file(), f"Missing imagexex dump in {out_dir}"
    verify_sha256sums_file(out_dir)

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    assert manifest["name"] == name
    assert manifest["baseline"]["sha256"] == BASELINE_SHA256
    assert manifest["baseline"]["verified_intact"] is True

    flags = manifest["flags"]
    assert flags["XDK_CONFIGURATION"] == "Release"
    assert flags["XDK_OPTIMIZATION"] == "aggressive"
    assert flags["XDK_WORKER_MAP"] == "current"
    assert flags["XDK_SLICE_DISPATCH"] == "legacy"
    assert flags["XDK_LOW_LATENCY_INPUT"] == "1"
    assert flags["XDK_H264_STAGE_PROFILE"] == str(expected_stage_profile)
    assert flags["XDK_INPUT_HW_THREAD"] == expected_input_hw

    assert manifest["ffmpeg_manifest"]["h264_stage_profile"] == expected_stage_profile
    patch_names = [p["name"] for p in manifest["patches"]]
    assert patch_names == expected_patches, f"{name} patches={patch_names} != {expected_patches}"

    dump_info = build_v026_experiment.parse_imagexex_dump(dump_path.read_text(encoding="utf-8"))
    assert dump_info["title_id"] == "4D583336", f"Unexpected Title ID in {name}: {dump_info}"
    assert dump_info["has_xbdm"] is False, f"Release build {name} must not import xbdm.xex"
    assert dump_info["embedded_spa_resource"] is True, f"Missing embedded SPA in {name}"
    assert manifest["imagexex_dump"] == dump_info

    return manifest


def test_all_three_experiment_builds() -> None:
    base_patches = list(build_v026_experiment.BATCH16_PATCHES)
    stage_patches = base_patches + [build_v026_experiment.UNIFIED_STAGE_PATCH]

    m_stage0 = verify_experiment_build("v026-stage0-profiler", 1, "3", stage_patches)
    m_hw3 = verify_experiment_build("v026-exp1-input-hw3", 0, "3", base_patches)
    m_hw5 = verify_experiment_build("v026-exp1-input-hw5", 0, "5", base_patches)

    # Confirm FFmpeg libraries are bit-identical between HW3 and HW5 A/B builds.
    for lib in ("avcodec.lib", "avutil.lib", "pthreads.lib"):
        h3 = m_hw3["artifacts"][f"ffmpeg/{lib}"]["sha256"]
        h5 = m_hw5["artifacts"][f"ffmpeg/{lib}"]["sha256"]
        assert h3 == h5, f"Expected identical {lib} between HW3 and HW5: {h3} != {h5}"

    # Confirm HW3 and HW5 XEX binaries differ cleanly from each other and from baseline/stage0.
    x3 = m_hw3["artifacts"]["default.xex"]["sha256"]
    x5 = m_hw5["artifacts"]["default.xex"]["sha256"]
    xs = m_stage0["artifacts"]["default.xex"]["sha256"]
    assert x3 != x5, "Expected HW3 and HW5 default.xex binaries to differ"
    assert xs != x3 and xs != x5, "Expected Stage 0 default.xex to differ from uninstrumented A/B builds"

    # Confirm Stage 0 patched FFmpeg source carries unified timebase & dispatch sync hooks.
    xb_stage0 = (XDK / "build" / "v026-stage0-profiler" / "ffmpeg-src" / "libavcodec" / "xb_thread.c").read_text(encoding="utf-8")
    assert "ff_xbox_slice_last_dispatch_sync_ticks" in xb_stage0
    assert "__mftb32()" in xb_stage0


if __name__ == "__main__":
    tests = (
        test_baseline_intact,
        test_build_py_env_and_manifest_guards,
        test_stage_trace_analyzer_schema1_and_schema2,
        test_all_three_experiment_builds,
    )
    for fn in tests:
        fn()
        print(f"PASS {fn.__name__}")
    print(f"ALL {len(tests)}/{len(tests)} v0.2.6 Stage 0 + Exp 1 tests passed")
