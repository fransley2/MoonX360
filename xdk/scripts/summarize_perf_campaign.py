"""Summarize offline and live MoonX360 performance campaign runs into JSON, CSV, and Markdown."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
import re


BENCH_LINE_RE = re.compile(
    r"^(?P<label>.+?)\s+threads=(?P<threads>\d+)\s+filter=(?P<filter>\S+)\s+"
    r"avg=(?P<avg>[0-9.]+)\s+p50=(?P<p50>[0-9.]+)\s+p95=(?P<p95>[0-9.]+)\s+"
    r"p99=(?P<p99>[0-9.]+)\s+max=(?P<max>[0-9.]+)\s+ms\s+"
    r"frames=(?P<frames>\d+)\s+mismatches=(?P<mismatches>\d+)\s+failed=(?P<failed>\d+)$"
)


def parse_kv_file(path: Path) -> dict[str, str]:
    if not path.is_file():
        return {}
    out: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "=" in line and "," not in line.split("=", 1)[0]:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def num(d: dict[str, str], key: str) -> float | None:
    val = d.get(key)
    if val is None:
        return None
    try:
        return float(val)
    except ValueError:
        return None


def parse_offline_dir(run_dir: Path) -> dict:
    bench_path = run_dir / "decoder-benchmark.txt"
    hashes_path = run_dir / "decoder-frame-hashes.txt"
    rows = {}
    if bench_path.is_file():
        for line in bench_path.read_text(encoding="utf-8", errors="replace").splitlines():
            m = BENCH_LINE_RE.match(line.strip())
            if m:
                gd = m.groupdict()
                rows[gd["label"]] = {
                    "threads": int(gd["threads"]),
                    "filter": gd["filter"],
                    "avg_ms": float(gd["avg"]),
                    "p50_ms": float(gd["p50"]),
                    "p95_ms": float(gd["p95"]),
                    "p99_ms": float(gd["p99"]),
                    "max_ms": float(gd["max"]),
                    "frames": int(gd["frames"]),
                    "mismatches": int(gd["mismatches"]),
                    "failed": int(gd["failed"]),
                }
    hashes_sha256 = hashlib.sha256(hashes_path.read_bytes()).hexdigest() if hashes_path.is_file() else None
    all_vmx = rows.get("All VMX C LF full", {})
    t4 = rows.get("Threads 4 FULL", {})
    return {
        "run": run_dir.name,
        "kind": "offline",
        "done": (run_dir / "benchmark-done.txt").is_file(),
        "hashes_sha256": hashes_sha256,
        "all_vmx_c_lf": all_vmx,
        "threads4_full": t4,
        "rows": rows,
    }


def parse_live_dir(run_dir: Path) -> dict:
    dec = parse_kv_file(run_dir / "decoder-session.txt")
    perf = parse_kv_file(run_dir / "performance-session.txt")
    fp = parse_kv_file(run_dir / "frame-pacing-session.txt")
    net = parse_kv_file(run_dir / "network-session.txt")
    stall = parse_kv_file(run_dir / "last-video-stall.txt")
    gpu_test = (run_dir / "gpu-cached-selftest.txt").read_text(encoding="utf-8", errors="replace").strip() if (run_dir / "gpu-cached-selftest.txt").is_file() else ""
    vmx_test = (run_dir / "vmx-selftest.txt").read_text(encoding="utf-8", errors="replace").splitlines()[-1] if (run_dir / "vmx-selftest.txt").is_file() else ""

    decode_avg_ms = (num(dec, "decode_avg_us") or (num(perf, "h264_avcodec_call_avg_us") or 0)) / 1000.0 if (dec or perf) else None
    decode_p50_ms = (num(perf, "h264_avcodec_call_p50_us") or num(fp, "decode_p50_us") or 0) / 1000.0 if (perf or fp) else None
    decode_p95_ms = (num(dec, "decode_p95_us") or num(perf, "h264_avcodec_call_p95_us") or num(fp, "decode_p95_us") or 0) / 1000.0 if (dec or perf or fp) else None
    decode_p99_ms = (num(dec, "decode_p99_us") or num(perf, "h264_avcodec_call_p99_us") or num(fp, "decode_p99_us") or 0) / 1000.0 if (dec or perf or fp) else None
    decode_max_ms = (num(dec, "decode_max_session_us") or num(perf, "h264_avcodec_call_max_us") or 0) / 1000.0 if (dec or perf) else None

    return {
        "run": run_dir.name,
        "kind": "live",
        "slice_dispatch": dec.get("slice_dispatch"),
        "slice_dispatch_version": dec.get("slice_dispatch_version"),
        "session_slices": num(dec, "session_slices"),
        "packet_size": num(net, "packet_size"),
        "pacing_mode": fp.get("pacing_mode"),
        "duration_s": (num(dec, "stream_duration_us") or 0) / 1e6 if dec.get("stream_duration_us") else None,
        "decode_avg_ms": decode_avg_ms,
        "decode_p50_ms": decode_p50_ms,
        "decode_p95_ms": decode_p95_ms,
        "decode_p99_ms": decode_p99_ms,
        "decode_max_ms": decode_max_ms,
        "yuv_avg_ms": (num(perf, "yuv_transfer_total_avg_us") / 1000.0) if perf.get("yuv_transfer_total_avg_us") else None,
        "yuv_p95_ms": (num(perf, "yuv_transfer_total_p95_us") / 1000.0) if perf.get("yuv_transfer_total_p95_us") else None,
        "yuv_p99_ms": (num(perf, "yuv_transfer_total_p99_us") / 1000.0) if perf.get("yuv_transfer_total_p99_us") else None,
        "q_depth_avg": num(perf, "video_decode_queue_depth_avg_count"),
        "q_depth_p95": num(perf, "video_decode_queue_depth_p95_count"),
        "q_age_p50_ms": (num(perf, "video_decode_queue_age_p50_us") / 1000.0) if perf.get("video_decode_queue_age_p50_us") else None,
        "q_age_p95_ms": (num(perf, "video_decode_queue_age_p95_us") / 1000.0) if perf.get("video_decode_queue_age_p95_us") else None,
        "frame_age_p50_ms": (num(fp, "frame_age_p50_us") / 1000.0) if fp.get("frame_age_p50_us") else None,
        "frame_age_p95_ms": (num(fp, "frame_age_p95_us") / 1000.0) if fp.get("frame_age_p95_us") else None,
        "frame_age_p99_ms": (num(fp, "frame_age_p99_us") / 1000.0) if fp.get("frame_age_p99_us") else None,
        "admit_vblank_p50_ms": (num(fp, "admit_to_vblank_p50_us") / 1000.0) if fp.get("admit_to_vblank_p50_us") else None,
        "admit_vblank_p95_ms": (num(fp, "admit_to_vblank_p95_us") / 1000.0) if fp.get("admit_to_vblank_p95_us") else None,
        "admit_vblank_p99_ms": (num(fp, "admit_to_vblank_p99_us") / 1000.0) if fp.get("admit_to_vblank_p99_us") else None,
        "rx_fps": num(dec, "received_callback_fps"),
        "dec_fps": num(dec, "decoded_fps"),
        "pres_fps": num(dec, "shown_fps"),
        "fp_fps": num(fp, "fps_presented"),
        "dropped": num(dec, "dropped"),
        "fp_dropped": num(fp, "dropped"),
        "fp_repeated": num(fp, "repeated"),
        "fp_late": num(fp, "late"),
        "decode_errors": num(dec, "decode_errors"),
        "recovery_count": num(dec, "recovery_count"),
        "fec_recovered": num(dec, "fec_recovered_packets") if dec.get("fec_recovered_packets") is not None else num(net, "fec_recovered_data_shards"),
        "fec_failed": num(dec, "fec_failed_data_deficit") if dec.get("fec_failed_data_deficit") is not None else num(net, "incomplete_block_data_deficit"),
        "rtp_oos": num(dec, "rtp_oos_packets"),
        "frame_loss_notif": num(dec, "frame_loss_notifications"),
        "decode_q_overflows": num(net, "decode_queue_overflows"),
        "input_int_avg_us": num(perf, "input_interval_avg_us"),
        "input_int_p95_us": num(perf, "input_interval_p95_us"),
        "input_int_p99_us": num(perf, "input_interval_p99_us"),
        "input_int_max_us": num(perf, "input_interval_max_us"),
        "input_poll_p95_us": num(perf, "input_poll_work_p95_us"),
        "audio_glitches": num(perf, "audio_glitches_total"),
        "audio_q_full": num(perf, "audio_queue_full_total"),
        "audio_submit_fail": num(perf, "audio_submit_failure_total"),
        "audio_buf_avg": num(perf, "audio_buffers_avg_count"),
        "audio_ms_avg": num(perf, "audio_queued_ms_avg_ms"),
        "cached_failovers": num(dec, "cached_failovers"),
        "gpu_starvation": num(dec, "gpu_slot_starvation"),
        "gpu_selftest": gpu_test,
        "vmx_selftest": vmx_test,
        "stall_mask": stall.get("classifications_mask"),
    }


def fmt(v: float | None, digits: int = 2) -> str:
    if v is None:
        return "-"
    return f"{v:.{digits}f}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("runs_dir", type=Path)
    ap.add_argument("--out-json", type=Path)
    ap.add_argument("--out-csv", type=Path)
    ap.add_argument("--out-md", type=Path)
    args = ap.parse_args()

    offline = []
    live = []
    for d in sorted(p for p in args.runs_dir.iterdir() if p.is_dir()):
        if (d / "decoder-benchmark.txt").is_file():
            offline.append(parse_offline_dir(d))
        elif (d / "decoder-session.txt").is_file() or (d / "performance-session.txt").is_file():
            live.append(parse_live_dir(d))

    data = {"offline": offline, "live": live}
    if args.out_json:
        args.out_json.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")

    if args.out_csv and live:
        keys = list(live[0].keys())
        with args.out_csv.open("w", newline="", encoding="utf-8") as fh:
            w = csv.DictWriter(fh, fieldnames=keys)
            w.writeheader()
            w.writerows(live)

    md_lines = ["## Offline Replay Summary\n"]
    if offline:
        md_lines.append("| Run | Done | VMX+C-LF Avg | P50 | P95 | P99 | Max | T4 P95 | Mismatches | Hash SHA256 (12) |")
        md_lines.append("|---|---|---:|---:|---:|---:|---:|---:|---:|---|")
        for r in offline:
            a = r["all_vmx_c_lf"]
            t4 = r["threads4_full"]
            md_lines.append(
                f"| {r['run']} | {r['done']} | {fmt(a.get('avg_ms'),3)} | {fmt(a.get('p50_ms'),3)} | "
                f"{fmt(a.get('p95_ms'),3)} | {fmt(a.get('p99_ms'),3)} | {fmt(a.get('max_ms'),3)} | "
                f"{fmt(t4.get('p95_ms'),3)} | {a.get('mismatches','-')} | `{(r['hashes_sha256'] or '-')[:12]}` |"
            )

    md_lines.append("\n## Live Sessions Summary\n")
    if live:
        md_lines.append("| Run | Dur(s) | Dec Avg/P50/P95/P99/Max (ms) | YUV P50/P95 (Avg) ms | QDepth Avg/P95 | Admit->VBlank P50/P95/P99 (ms) | RX/Dec/Pres FPS | Drop/Err/FECFail | InpP95/P99 (us) | AudioGlitch/BufAvg |")
        md_lines.append("|---|---:|---|---|---|---:|---|---|---:|---|")
        for r in live:
            dec_s = f"{fmt(r['decode_avg_ms'])}/{fmt(r['decode_p50_ms'])}/{fmt(r['decode_p95_ms'])}/{fmt(r['decode_p99_ms'])}/{fmt(r['decode_max_ms'])}"
            yuv_s = f"{fmt(r['yuv_avg_ms'])}/{fmt(r['yuv_p95_ms'])}"
            qd_s = f"{fmt(r['q_depth_avg'])}/{fmt(r['q_depth_p95'],1)}"
            avb_s = f"{fmt(r['admit_vblank_p50_ms'])}/{fmt(r['admit_vblank_p95_ms'])}/{fmt(r['admit_vblank_p99_ms'])}"
            fps_s = f"{fmt(r['rx_fps'])}/{fmt(r['dec_fps'])}/{fmt(r['pres_fps'])}"
            err_s = f"{fmt(r['dropped'],0)}/{fmt(r['decode_errors'],0)}/{fmt(r['fec_failed'],0)}"
            inp_s = f"{fmt(r['input_int_p95_us'],0)}/{fmt(r['input_int_p99_us'],0)}"
            aud_s = f"{fmt(r['audio_glitches'],0)}/{fmt(r['audio_buf_avg'])}"
            md_lines.append(f"| {r['run']} | {fmt(r['duration_s'],1)} | {dec_s} | {yuv_s} | {qd_s} | {avb_s} | {fps_s} | {err_s} | {inp_s} | {aud_s} |")

    md = "\n".join(md_lines) + "\n"
    if args.out_md:
        args.out_md.write_text(md, encoding="utf-8")
    else:
        print(md)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
