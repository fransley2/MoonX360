"""Build disposable-key MoonX360 test packages N, N+1, and fail-boot N+2 inside .work/update-test-kit/."""
import argparse
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / ".work" / "update-test-kit"


def run_build(out_dir: Path, ffmpeg_build: Path, version: str, build_id: int,
              test_cfg: Path, fail_boot: bool = False) -> tuple[Path, Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    # Reuse validated Final-release FFmpeg libraries (v026-rc2 + batch16) and third-party static libs cleanly.
    for sub in ("ffmpeg", "ffmpeg-xbox", "mbedtls.lib", "opus.lib", "compat.lib", "moonlight.lib"):
        src = ffmpeg_build / sub
        dst = out_dir / sub
        if src.exists() and not dst.exists():
            if src.is_dir():
                shutil.copytree(src, dst)
            else:
                shutil.copy2(src, dst)
    compat_dir = out_dir / "compat"
    if not compat_dir.exists() and (ffmpeg_build / "compat").exists():
        shutil.copytree(ffmpeg_build / "compat", compat_dir)

    env = dict(
        os.environ,
        XDK_BUILD_DIR=str(out_dir),
        XDK_FFMPEG_ROOT=str(ffmpeg_build / "ffmpeg-src"),
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
        MOONX_VERSION=version,
        MOONX_BUILD_ID=str(build_id),
        MOONX_LAUNCHER_VERSION="1",
        MOONX_UPDATE_TEST_CONFIG=str(test_cfg),
        MOONX_UPDATE_TEST_FAIL_BOOT="1" if fail_boot else "0",
    )
    if (out_dir / "ffmpeg-xbox").is_dir():
        env["XDK_FFMPEG_XBOX_DIR"] = str(out_dir / "ffmpeg-xbox")
    subprocess.run(
        [sys.executable, str(ROOT / "xdk/build.py"), "app", "xex", "launcher", "shortcut_xex"],
        cwd=ROOT, env=env, check=True,
    )
    return out_dir / "default.xex", out_dir / "moonx360.xex"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-url", default="http://192.168.15.7:8266/",
                        help="Local test server base URL reachable from Xbox 360")
    parser.add_argument("--ffmpeg-build", type=Path,
                        default=ROOT / "xdk" / "build" / "perf-campaign-20261126" / "Final-release",
                        help="Existing validated Final-release FFmpeg build directory")
    parser.add_argument("--clean", action="store_true", help="Rebuild .work/update-test-kit from scratch")
    args = parser.parse_args()

    if args.clean and WORK.exists():
        def _retry_rm(fn, p, _exc):
            os.chmod(p, stat.S_IWRITE | stat.S_IREAD)
            time.sleep(0.05)
            fn(p)
        for attempt in range(4):
            try:
                shutil.rmtree(WORK, onexc=_retry_rm)
                break
            except OSError:
                if attempt == 3:
                    raise
                time.sleep(0.25)
    WORK.mkdir(parents=True, exist_ok=True)

    base_url = args.base_url if args.base_url.endswith("/") else args.base_url + "/"
    key_pem = WORK / "disposable-test-ed25519.pem"
    test_cfg = WORK / "test_update_config.h"

    subprocess.run(
        [
            sys.executable, str(ROOT / "tools/generate_update_key.py"),
            "--key", str(key_pem),
            "--test-config-header", str(test_cfg),
            "--test-base-url", base_url,
            "--allow-in-repo", "--force",
        ],
        check=True,
    )

    n_launcher, n_app = run_build(WORK / "build-N", args.ffmpeg_build, "0.2.7-test1", 100, test_cfg, False)
    _, n1_app = run_build(WORK / "build-N1", args.ffmpeg_build, "0.2.8-test2", 101, test_cfg, False)
    _, n2_fail = run_build(WORK / "build-N2-fail", args.ffmpeg_build, "0.2.9-testfail", 102, test_cfg, True)

    pkg_n = WORK / "package-N-install"
    pkg_n.mkdir(parents=True, exist_ok=True)
    for stale in ("moonx360.xex",):
        if (pkg_n / stale).exists():
            (pkg_n / stale).unlink()
    shutil.copy2(n_launcher, pkg_n / "default.xex")

    srv = WORK / "server"
    for ch in ("stable", "beta"):
        (srv / ch).mkdir(parents=True, exist_ok=True)
    (srv / "assets").mkdir(parents=True, exist_ok=True)
    shutil.copy2(n1_app, srv / "assets" / "moonx360-v0.2.8-test2.xex")
    shutil.copy2(n2_fail, srv / "assets" / "moonx360-v0.2.9-testfail.xex")

    # Stable offers N+1 (build 101); Beta offers N+2 fail-boot (build 102) for rollback test.
    for ch, xex_name, ver, bid, notes in [
        ("stable", "moonx360-v0.2.8-test2.xex", "0.2.8-test2", 101,
         ("Test update N+1 (build 101).", "Atualizacao de teste N+1 (build 101).", "Actualizacion de prueba N+1 (build 101).")),
        ("beta", "moonx360-v0.2.9-testfail.xex", "0.2.9-testfail", 102,
         ("Simulated unconfirmed boot N+2 (build 102).", "Simulacao de falha de confirmacao N+2 (build 102).", "Simulacion de arranque sin confirmar N+2 (build 102).")),
    ]:
        manifest_path = srv / ch / "manifest.json"
        subprocess.run(
            [
                sys.executable, str(ROOT / "tools/generate_update_manifest.py"),
                str(srv / "assets" / xex_name),
                "--version", ver,
                "--build-id", str(bid),
                "--channel", ch,
                "--min-launcher-version", "1",
                "--url", f"{base_url}assets/{xex_name}",
                "--allow-test-url",
                "--notes-en", notes[0],
                "--notes-pt-br", notes[1],
                "--notes-es", notes[2],
                "--output", str(manifest_path),
            ],
            check=True,
        )
        subprocess.run(
            [
                sys.executable, str(ROOT / "tools/sign_update_manifest.py"),
                "sign", str(manifest_path), "--key", str(key_pem),
            ],
            check=True,
        )

    (WORK / "serve_test_updates.py").write_text(
        '"""Local HTTP range-capable update server for disposable MoonX360 console bench tests."""\n'
        "import http.server\nimport os\nfrom pathlib import Path\nimport re\nimport sys\n\n"
        "SERVE_DIR = Path(__file__).resolve().parent / 'server'\n"
        "PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8266\n\n"
        "class RangeHandler(http.server.SimpleHTTPRequestHandler):\n"
        "    def __init__(self, *a, **kw): super().__init__(*a, directory=str(SERVE_DIR), **kw)\n"
        "    def send_head(self):\n"
        "        path = Path(self.translate_path(self.path))\n"
        "        rng = self.headers.get('Range')\n"
        "        if rng and path.is_file():\n"
        "            m = re.match(r'bytes=(\\d+)-(\\d*)', rng)\n"
        "            if m:\n"
        "                data = path.read_bytes()\n"
        "                start = int(m.group(1))\n"
        "                end = int(m.group(2)) if m.group(2) else len(data) - 1\n"
        "                end = min(end, len(data) - 1)\n"
        "                chunk = data[start:end+1]\n"
        "                self.send_response(206)\n"
        "                self.send_header('Content-Range', f'bytes {start}-{end}/{len(data)}')\n"
        "                self.send_header('Content-Length', str(len(chunk)))\n"
        "                self.send_header('Content-Type', 'application/octet-stream')\n"
        "                self.end_headers()\n"
        "                return __import__('io').BytesIO(chunk)\n"
        "        return super().send_head()\n\n"
        "if __name__ == '__main__':\n"
        "    print(f'Serving {SERVE_DIR} on 0.0.0.0:{PORT}')\n"
        "    http.server.ThreadingHTTPServer(('0.0.0.0', PORT), RangeHandler).serve_forever()\n",
        encoding="utf-8",
    )

    print(json.dumps({
        "work_dir": str(WORK),
        "package_N": str(pkg_n),
        "stable_N1": str(srv / "stable" / "manifest.json"),
        "beta_N2_fail": str(srv / "beta" / "manifest.json"),
        "server_script": str(WORK / "serve_test_updates.py"),
    }, indent=2))


if __name__ == "__main__":
    main()
