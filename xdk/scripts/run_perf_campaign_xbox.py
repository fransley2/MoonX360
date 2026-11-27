r"""Automated Xbox 360 XBDM runner and metrics collector for MoonX360 optimization campaign.

Supports:
- offline: runs 600-frame deterministic MLC1 replay in isolated HDD:\Apps\MoonX360-PerfBench\
- live:    runs an auto-live-test stream from isolated HDD:\Apps\MoonX360\perf-<tag>.xex
           (NEVER modifies HDD:\Apps\MoonX360\default.xex or moonlight.ini/certs)
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import socket
import struct
import time

from collect_latency_xbdm import FILES, getfile, read_exact, read_line
from xbox_snapshot_xbdm import command


AURORA_XEX = r"HDD:\Apps\Aurora\Aurora.xex"
AURORA_DIR = r"HDD:\Apps\Aurora\\"[:-1]
BENCH_DIR = r"HDD:\Apps\MoonX360-PerfBench"
BENCH_MOONLIGHT = BENCH_DIR + r"\moonlight"
LIVE_DIR = r"HDD:\Apps\MoonX360"
LIVE_MOONLIGHT = LIVE_DIR + r"\moonlight"

EXTRA_REPORTS = (
    "benchmark-done.txt",
    "frame-pacing-session.txt",
    "performance-menu.txt",
    "video-decoded.txt",
)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def current_title(console: str) -> str | None:
    lines = command(console, "modules", True)
    matches = [line for line in lines if "base=0x82000000" in line]
    if not matches:
        return None
    m = re.search(r'name="([^"]+)"', matches[0])
    return m.group(1) if m else None


def exec_state(console: str) -> str:
    return command(console, "getexecstate")


def remote_entries(console: str, folder: str) -> dict[str, dict]:
    entries = {}
    for line in command(console, f'dirlist name="{folder}"', True):
        m = re.search(r'name="([^"]+)" sizehi=0x([0-9a-fA-F]+) sizelo=0x([0-9a-fA-F]+)', line)
        if m:
            entries[m.group(1).lower()] = {
                "name": m.group(1),
                "size": (int(m.group(2), 16) << 32) | int(m.group(3), 16),
                "dir": "directory" in line,
            }
    return entries


def ensure_dir(console: str, folder: str) -> None:
    try:
        command(console, f'mkdir name="{folder}"')
    except OSError as exc:
        msg = str(exc).lower()
        if "already exists" not in msg and "414" not in msg:
            # Verify via dirlist
            remote_entries(console, folder)
            return
    remote_entries(console, folder)


def delete_if_allowed(console: str, remote_path: str) -> None:
    low = remote_path.lower()
    protected = (
        r"hdd:\apps\moonx360\default.xex",
        r"hdd:\apps\moonx360\moonlight\moonlight.ini",
        r"hdd:\apps\moonx360\moonlight\moonlight.ini.bak",
        r"hdd:\apps\moonx360\moonlight\client.pem",
        r"hdd:\apps\moonx360\moonlight\key.pem",
        r"hdd:\apps\moonx360\moonlight\hosts.tsv",
    )
    if low in protected:
        raise ValueError(f"Refusing to delete protected console file: {remote_path}")
    try:
        command(console, f'delete name="{remote_path}"')
    except OSError:
        pass


def send_bytes(console: str, data: bytes, remote_path: str, timeout: int = 45) -> dict:
    folder, fname = remote_path.rsplit("\\", 1)
    delete_if_allowed(console, remote_path)
    with socket.create_connection((console, 730), timeout=8) as sock:
        sock.settimeout(timeout)
        if not read_line(sock).startswith(b"201"):
            raise OSError("Unexpected XBDM greeting")
        sock.sendall(f'sendfile name="{remote_path}" length=0x{len(data):x}\r\n'.encode("ascii"))
        status = read_line(sock)
        if not status.startswith(b"204"):
            raise OSError("XBDM sendfile: " + status.decode("ascii", "replace").strip())
        mv = memoryview(data)
        sent = 0
        while sent < len(data):
            chunk = mv[sent:sent + 65536]
            sock.sendall(chunk)
            sent += len(chunk)
        completion = read_line(sock)
        if not completion.startswith(b"200"):
            raise OSError("XBDM sendfile completion: " + completion.decode("ascii", "replace").strip())
    entries = remote_entries(console, folder)
    info = entries.get(fname.lower())
    if not info or info["size"] != len(data):
        raise OSError(f"Remote size mismatch after upload for {remote_path}: {info}")
    return {"remote": remote_path, "bytes": len(data), "sha256": sha256_bytes(data)}


def send_file(console: str, local_path: Path, remote_path: str, skip_if_same_size: bool = False) -> dict:
    data = local_path.read_bytes()
    if remote_path.lower().endswith(".xex") and data[:4] != b"XEX2":
        raise ValueError(f"Not a valid XEX2 binary: {local_path}")
    folder, fname = remote_path.rsplit("\\", 1)
    if skip_if_same_size:
        try:
            entries = remote_entries(console, folder)
            info = entries.get(fname.lower())
            if info and info["size"] == len(data):
                return {"remote": remote_path, "bytes": len(data), "sha256": sha256_bytes(data), "skipped_same_size": True}
        except OSError:
            pass
    return send_bytes(console, data, remote_path)


def magicboot(console: str, xex_path: str) -> str:
    directory = xex_path.rsplit("\\", 1)[0] + "\\"
    return command(console, f'magicboot title="{xex_path}" directory="{directory}"')


def ensure_aurora_idle(console: str, timeout_s: int = 25) -> None:
    st = exec_state(console)
    if "reboot_title" in st.lower():
        raise RuntimeError(f"Console in unsafe state before launch: {st}")
    title = current_title(console)
    if title and title.lower() == "aurora.xex":
        return
    print(f"[XBDM] Switching console from {title} to Aurora...", flush=True)
    magicboot(console, AURORA_XEX)
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        time.sleep(1.5)
        try:
            st = exec_state(console)
            t = current_title(console)
            if "200- start" in st.lower() and t and t.lower() == "aurora.xex":
                time.sleep(1.5)
                return
        except OSError:
            pass
    raise RuntimeError(f"Failed to settle on Aurora cleanly (last title={current_title(console)} state={exec_state(console)})")


def wait_for_launch_and_return(console: str, expected_exe: str, timeout_s: int, poll_s: float = 2.0) -> dict:
    t0 = time.monotonic()
    saw_expected = False
    max_wait_start = t0 + 20.0
    observations = []
    # 1. Confirm expected title boots
    while time.monotonic() < max_wait_start:
        time.sleep(1.5)
        try:
            st = exec_state(console)
            t = current_title(console)
            observations.append({"t": round(time.monotonic() - t0, 2), "title": t, "state": st})
            if "reboot_title" in st.lower() or "stop" in st.lower():
                raise RuntimeError(f"Aborted: unsafe exec state during launch: {st} title={t}")
            if t and t.lower() == expected_exe.lower():
                saw_expected = True
                break
            if saw_expected and t and t.lower() == "aurora.xex":
                # Finished very fast (e.g. cached benchmark)
                return {"saw_expected": True, "elapsed_s": round(time.monotonic() - t0, 2), "observations": observations}
        except OSError as exc:
            observations.append({"t": round(time.monotonic() - t0, 2), "err": str(exc)})

    if not saw_expected:
        # Maybe it finished in <1.5s and is already back at Aurora!
        t = current_title(console)
        if t and t.lower() == "aurora.xex":
            return {"saw_expected": False, "returned_early": True, "elapsed_s": round(time.monotonic() - t0, 2), "observations": observations}
        raise RuntimeError(f"Expected title {expected_exe} never confirmed (observations={observations})")

    # 2. Wait for clean return to Aurora
    deadline = t0 + timeout_s
    reboot_since = None
    while time.monotonic() < deadline:
        time.sleep(poll_s)
        try:
            st = exec_state(console)
            t = current_title(console)
            if "reboot_title" in st.lower():
                if reboot_since is None:
                    reboot_since = time.monotonic()
                elif time.monotonic() - reboot_since > 20.0:
                    raise RuntimeError(f"Console stuck in {st} for >20s while returning from {expected_exe}!")
                continue
            reboot_since = None
            if t and t.lower() == "aurora.xex" and "200- start" in st.lower():
                time.sleep(1.5)
                return {"saw_expected": True, "elapsed_s": round(time.monotonic() - t0, 2), "observations": observations}
        except OSError:
            pass
    raise RuntimeError(f"Timed out after {timeout_s}s waiting for {expected_exe} to exit to Aurora")


def download_reports(console: str, remote_folder: str, output_dir: Path, names: tuple[str, ...]) -> dict:
    output_dir.mkdir(parents=True, exist_ok=True)
    avail = remote_entries(console, remote_folder)
    downloaded = {}
    for name in names:
        if name.lower() not in avail:
            continue
        try:
            data = getfile(console, remote_folder + "\\" + name, max_bytes=4 * 1024 * 1024)
            (output_dir / name).write_bytes(data)
            downloaded[name] = {"bytes": len(data), "sha256": sha256_bytes(data)}
        except (OSError, ValueError) as exc:
            downloaded[name] = {"error": str(exc)}
    return downloaded


def parse_kv(text: str) -> dict[str, str]:
    out = {}
    for line in text.splitlines():
        if "=" in line and "," not in line.split("=", 1)[0]:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def parse_latency_stats(text: str) -> dict:
    res = {}
    for line in text.splitlines():
        m = re.match(r"^(\S+)\s+n=(\d+)\s+current=(\d+)\s+ema=(\d+)\s+p50=(\d+)\s+p95=(\d+)\s+p99=(\d+)\s+max=(\d+)$", line.strip())
        if m:
            res[m.group(1)] = {
                "n": int(m.group(2)),
                "current": int(m.group(3)),
                "ema": int(m.group(4)),
                "p50": int(m.group(5)),
                "p95": int(m.group(6)),
                "p99": int(m.group(7)),
                "max": int(m.group(8)),
            }
        elif "=" in line:
            k, v = line.split("=", 1)
            res[k.strip()] = v.strip()
    return res


def summarize_live_run(out_dir: Path) -> dict:
    summary = {"dir": str(out_dir)}
    for fname in ("decoder-session.txt", "frame-pacing-session.txt", "h264-parallel-session.txt",
                  "network-session.txt", "last-session.txt", "seamless-session.txt"):
        p = out_dir / fname
        if p.is_file():
            summary[fname] = parse_kv(p.read_text(encoding="utf-8", errors="replace"))
    lat_p = out_dir / "latency-capture.txt"
    if lat_p.is_file():
        summary["latency-capture.txt"] = parse_latency_stats(lat_p.read_text(encoding="utf-8", errors="replace"))
    return summary


def run_offline(args) -> int:
    ensure_aurora_idle(args.console)
    ensure_dir(args.console, BENCH_DIR)
    ensure_dir(args.console, BENCH_MOONLIGHT)
    xex_info = send_file(args.console, args.xex, BENCH_DIR + r"\default.xex", skip_if_same_size=False)
    cap_info = send_file(args.console, args.capture, BENCH_MOONLIGHT + r"\capture.bin", skip_if_same_size=True)
    info_path = args.capture.parent / "capture-info.txt"
    if info_path.is_file():
        send_file(args.console, info_path, BENCH_MOONLIGHT + r"\capture-info.txt", skip_if_same_size=False)
    delete_if_allowed(args.console, BENCH_MOONLIGHT + r"\benchmark-done.txt")
    delete_if_allowed(args.console, BENCH_MOONLIGHT + r"\decoder-benchmark.txt")
    delete_if_allowed(args.console, BENCH_MOONLIGHT + r"\decoder-frame-hashes.txt")
    send_bytes(args.console, b"1\n", BENCH_MOONLIGHT + r"\run-benchmark-exit.once")
    send_bytes(args.console, b"1\n", BENCH_MOONLIGHT + r"\auto-benchmark.once")

    launch_reply = magicboot(args.console, BENCH_DIR + r"\default.xex")
    wait_info = wait_for_launch_and_return(args.console, "default.xex", timeout_s=args.timeout)
    reports = download_reports(args.console, BENCH_MOONLIGHT, args.out, ("decoder-benchmark.txt", "decoder-frame-hashes.txt", "benchmark-done.txt", "vmx-selftest.txt", "direct-launch.log", "last-log.txt"))
    meta = {
        "mode": "offline",
        "collected_utc": datetime.now(timezone.utc).isoformat(),
        "xex": xex_info,
        "capture": cap_info,
        "launch_reply": launch_reply,
        "wait": wait_info,
        "reports": reports,
    }
    (args.out / "run-manifest.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
    bench_txt = args.out / "decoder-benchmark.txt"
    if not bench_txt.is_file():
        raise RuntimeError(f"Missing decoder-benchmark.txt in {args.out}")
    print(bench_txt.read_text(encoding="utf-8", errors="replace"))
    return 0


def run_live(args) -> int:
    ensure_aurora_idle(args.console)
    remote_exe_name = f"perf-{args.tag}.xex"
    remote_exe = LIVE_DIR + "\\" + remote_exe_name
    xex_info = send_file(args.console, args.xex, remote_exe, skip_if_same_size=False)

    cfg_lines = [
        f"duration_sec={args.duration}",
        f"width={args.width}",
        f"height={args.height}",
        f"fps={args.fps}",
        f"bitrate_kbps={args.bitrate}",
        f"vsync={args.vsync}",
        f"fast_decode=0",
        f"gpu_post_deblock={args.gpu_post_deblock}",
        f"diagnostic_log=1",
        f"session_report=1",
        f"stats_overlay={args.stats_overlay}",
        f"overlay_mode={args.overlay_mode}",
        f"slice_count={args.slices}",
        f"packet_size={args.packet_size}",
        f"frame_pacing_mode={args.frame_pacing}",
        f"capture_arm={args.capture_arm}",
    ]
    for stale_name in (
        "decoder-session.txt", "frame-pacing-session.txt", "network-session.txt",
        "performance-session.txt", "last-session.txt", "latency-capture.txt",
        "seamless-session.txt", "smoothing-session.txt", "last-video-stall.txt",
        "last-log.txt", "connection-timing.txt",
    ):
        delete_if_allowed(args.console, LIVE_MOONLIGHT + "\\" + stale_name)
    if args.capture_arm:
        delete_if_allowed(args.console, LIVE_MOONLIGHT + r"\capture.bin")
        delete_if_allowed(args.console, LIVE_MOONLIGHT + r"\capture-info.txt")

    send_bytes(args.console, ("\n".join(cfg_lines) + "\n").encode("ascii"), LIVE_MOONLIGHT + r"\auto-live-test.once")
    launch_reply = magicboot(args.console, remote_exe)
    wait_info = wait_for_launch_and_return(args.console, remote_exe_name, timeout_s=args.duration + 90)
    names = tuple(dict.fromkeys((*FILES, *EXTRA_REPORTS)))
    reports = download_reports(args.console, LIVE_MOONLIGHT, args.out, names)
    if args.capture_arm:
        # Also fetch capture.bin separately (may exceed 4 MiB, up to 40 MiB)
        avail = remote_entries(args.console, LIVE_MOONLIGHT)
        if "capture.bin" in avail:
            cap_data = getfile(args.console, LIVE_MOONLIGHT + r"\capture.bin", max_bytes=40 * 1024 * 1024)
            (args.out / "capture.bin").write_bytes(cap_data)
            reports["capture.bin"] = {"bytes": len(cap_data), "sha256": sha256_bytes(cap_data)}
    # Clean up temporary isolated XEX to keep HDD:\Apps\MoonX360 tidy if requested
    if args.cleanup_xex:
        delete_if_allowed(args.console, remote_exe)
    summary = summarize_live_run(args.out)
    meta = {
        "mode": "live",
        "collected_utc": datetime.now(timezone.utc).isoformat(),
        "tag": args.tag,
        "xex": xex_info,
        "config": cfg_lines,
        "launch_reply": launch_reply,
        "wait": wait_info,
        "reports": reports,
        "summary": summary,
    }
    (args.out / "run-manifest.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_off = sub.add_parser("offline")
    p_off.add_argument("--console", default="192.168.15.10")
    p_off.add_argument("--xex", type=Path, required=True)
    p_off.add_argument("--capture", type=Path, required=True)
    p_off.add_argument("--out", type=Path, required=True)
    p_off.add_argument("--timeout", type=int, default=240)

    p_live = sub.add_parser("live")
    p_live.add_argument("--console", default="192.168.15.10")
    p_live.add_argument("--xex", type=Path, required=True)
    p_live.add_argument("--tag", required=True)
    p_live.add_argument("--out", type=Path, required=True)
    p_live.add_argument("--duration", type=int, default=120)
    p_live.add_argument("--width", type=int, default=1280)
    p_live.add_argument("--height", type=int, default=720)
    p_live.add_argument("--fps", type=int, default=60)
    p_live.add_argument("--bitrate", type=int, default=10000)
    p_live.add_argument("--vsync", type=int, default=1)
    p_live.add_argument("--slices", type=int, default=8)
    p_live.add_argument("--packet-size", type=int, default=1024)
    p_live.add_argument("--frame-pacing", type=int, default=0)
    p_live.add_argument("--gpu-post-deblock", type=int, default=0)
    p_live.add_argument("--stats-overlay", type=int, default=1)
    p_live.add_argument("--overlay-mode", type=int, default=3)
    p_live.add_argument("--capture-arm", type=int, default=0)
    p_live.add_argument("--cleanup-xex", action="store_true")

    args = parser.parse_args()
    if args.out.exists():
        raise SystemExit(f"Output directory already exists: {args.out}")
    if args.cmd == "offline":
        return run_offline(args)
    return run_live(args)


if __name__ == "__main__":
    raise SystemExit(main())
