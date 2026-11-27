"""Compile and execute the MoonX360 native auto-update host test suite against a local TLS 1.2 HTTPS server."""
import datetime
import hashlib
import http.server
import ipaddress
import json
import os
from pathlib import Path
import re
import shutil
import ssl
import subprocess
import sys
import tempfile
import threading

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / ".work" / "host-update-test"


def generate_ca_and_server_cert(folder: Path) -> tuple[Path, Path, Path]:
    now = datetime.datetime.now(datetime.timezone.utc)
    ca_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    ca_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "MoonX360 Test CA")])
    ca_cert = (
        x509.CertificateBuilder()
        .subject_name(ca_name)
        .issuer_name(ca_name)
        .public_key(ca_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(days=1))
        .not_valid_after(now + datetime.timedelta(days=30))
        .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
        .sign(ca_key, hashes.SHA256())
    )
    ca_pem = folder / "test-ca.pem"
    ca_pem.write_bytes(ca_cert.public_bytes(serialization.Encoding.PEM))

    srv_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    srv_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "localhost")])
    srv_cert = (
        x509.CertificateBuilder()
        .subject_name(srv_name)
        .issuer_name(ca_name)
        .public_key(srv_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(days=1))
        .not_valid_after(now + datetime.timedelta(days=30))
        .add_extension(
            x509.SubjectAlternativeName([
                x509.DNSName("localhost"),
                x509.IPAddress(ipaddress.IPv4Address("127.0.0.1")),
            ]),
            critical=False,
        )
        .sign(ca_key, hashes.SHA256())
    )
    srv_pem = folder / "server.pem"
    srv_key_pem = folder / "server.key"
    srv_pem.write_bytes(srv_cert.public_bytes(serialization.Encoding.PEM))
    srv_key_pem.write_bytes(
        srv_key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption())
    )

    bad_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    bad_cert = (
        x509.CertificateBuilder()
        .subject_name(srv_name)
        .issuer_name(srv_name)
        .public_key(bad_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(days=1))
        .not_valid_after(now + datetime.timedelta(days=30))
        .sign(bad_key, hashes.SHA256())
    )
    bad_pem = folder / "bad.pem"
    bad_key_pem = folder / "bad.key"
    bad_pem.write_bytes(bad_cert.public_bytes(serialization.Encoding.PEM))
    bad_key_pem.write_bytes(
        bad_key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption())
    )
    return ca_pem, srv_pem, srv_key_pem, bad_pem, bad_key_pem


def find_vcvars64() -> str:
    explicit = os.environ.get("VS_VCVARS64")
    if explicit and Path(explicit).is_file():
        return explicit
    for cand in (
        r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
        r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
        r"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat",
    ):
        if Path(cand).is_file():
            return cand
    raise SystemExit("vcvars64.bat not found")


def get_msvc_env() -> dict[str, str]:
    clean_env = dict(os.environ)
    clean_env["PATH"] = r"C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem"
    vcvars = find_vcvars64()
    r = subprocess.run(f'cmd.exe /d /s /c ""{vcvars}" >nul && set"',
                       env=clean_env, capture_output=True, text=True, check=True)
    out = dict(os.environ)
    for line in r.stdout.splitlines():
        if "=" in line and not line.startswith("="):
            k, v = line.split("=", 1)
            out[k.upper()] = v
    return out


def make_test_xex(path: Path, tag: int, size: int) -> None:
    import struct
    buf = bytearray([tag] * size)
    buf[0:4] = b"XEX2"
    struct.pack_into(">I", buf, 20, 1)
    struct.pack_into(">II", buf, 24, 0x40006, 36)
    buf[36 + 12 : 36 + 16] = bytes.fromhex("4D583336")
    path.write_bytes(bytes(buf))


def main() -> None:
    if WORK.exists():
        shutil.rmtree(WORK)
    WORK.mkdir(parents=True)

    ca_pem, srv_pem, srv_key_pem, bad_pem, bad_key_pem = generate_ca_and_server_cert(WORK)
    priv_pem = WORK / "test-ed25519.pem"
    res = subprocess.run(
        [
            sys.executable, str(ROOT / "tools/generate_update_key.py"),
            "--key", str(priv_pem), "--allow-in-repo", "--force",
        ],
        capture_output=True, text=True, check=True,
    )
    pub_hex = next(line.split(": ")[1].strip() for line in res.stdout.splitlines() if line.startswith("Public key hex:"))

    srv_dir = WORK / "srv"
    (srv_dir / "stable").mkdir(parents=True)
    (srv_dir / "beta").mkdir(parents=True)
    (srv_dir / "assets").mkdir(parents=True)

    p101 = srv_dir / "assets" / "101.xex"
    p102 = srv_dir / "assets" / "102.xex"
    make_test_xex(p101, 0xA1, 600 * 1024)
    make_test_xex(p102, 0xB2, 64 * 1024)

    flaky_calls = {"count": 0}

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            pass

        def do_GET(self):
            if self.path == "/redirect-evil":
                self.send_response(302)
                self.send_header("Location", "https://evil.example.com/moonx360.xex")
                self.end_headers()
                return
            if self.path == "/redirect-loop":
                self.send_response(302)
                self.send_header("Location", f"{base_url}redirect-loop")
                self.end_headers()
                return
            if self.path == "/flaky-asset":
                flaky_calls["count"] += 1
                if flaky_calls["count"] == 2:
                    self.send_response(500)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                target = p101
            else:
                rel = self.path.lstrip("/").split("?")[0]
                target = srv_dir / rel
            if not target.is_file():
                self.send_response(404)
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            data = target.read_bytes()
            rng = self.headers.get("Range")
            if rng:
                m = re.match(r"bytes=(\d+)-(\d*)", rng)
                if m:
                    s = int(m.group(1))
                    e = min(int(m.group(2)) if m.group(2) else len(data) - 1, len(data) - 1)
                    chunk = data[s : e + 1]
                    self.send_response(206)
                    self.send_header("Content-Range", f"bytes {s}-{e}/{len(data)}")
                    self.send_header("Content-Length", str(len(chunk)))
                    self.end_headers()
                    self.wfile.write(chunk)
                    return
            self.send_response(200)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    good_httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    good_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    good_ctx.load_cert_chain(certfile=str(srv_pem), keyfile=str(srv_key_pem))
    good_httpd.socket = good_ctx.wrap_socket(good_httpd.socket, server_side=True)
    port = good_httpd.server_address[1]
    base_url = f"https://localhost:{port}/"

    bad_httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    bad_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    bad_ctx.load_cert_chain(certfile=str(bad_pem), keyfile=str(bad_key_pem))
    bad_httpd.socket = bad_ctx.wrap_socket(bad_httpd.socket, server_side=True)
    bad_port = bad_httpd.server_address[1]

    for ch, xex, ver, bid in [
        ("stable", p101, "0.2.8", 101),
        ("beta", p102, "0.2.9-beta", 102),
    ]:
        mpath = srv_dir / ch / "manifest.json"
        subprocess.run(
            [
                sys.executable, str(ROOT / "tools/generate_update_manifest.py"),
                str(xex), "--version", ver, "--build-id", str(bid), "--channel", ch,
                "--min-launcher-version", "1", "--url", f"{base_url}assets/{xex.name}",
                "--allow-test-url",
                "--notes-en", f"Release {ver}", "--notes-pt-br", f"Versao {ver}", "--notes-es", f"Version {ver}",
                "--output", str(mpath),
            ],
            check=True,
        )
        subprocess.run(
            [sys.executable, str(ROOT / "tools/sign_update_manifest.py"), "sign", str(mpath), "--key", str(priv_pem)],
            check=True,
        )

    t_good = threading.Thread(target=good_httpd.serve_forever, daemon=True)
    t_bad = threading.Thread(target=bad_httpd.serve_forever, daemon=True)
    t_good.start()
    t_bad.start()

    try:
        env = get_msvc_env()
        cl = next(str(Path(p) / "cl.exe") for p in env["PATH"].split(";") if (Path(p) / "cl.exe").exists())
        mbedtls_dir = ROOT / "xdk" / "deps" / "mbedtls-2.16.12"
        mbedtls_srcs = sorted((mbedtls_dir / "library").glob("*.c"))
        mbedtls_objs = []
        obj_dir = WORK / "objs"
        obj_dir.mkdir(parents=True, exist_ok=True)
        for s in mbedtls_srcs:
            obj = obj_dir / (s.stem + ".obj")
            subprocess.run(
                [cl, "/nologo", "/c", "/O2", "/W3", "/D_CRT_SECURE_NO_WARNINGS",
                 "/I" + str(mbedtls_dir / "include"), "/TC", str(s), "/Fo" + str(obj)],
                env=env, check=True, stdout=subprocess.DEVNULL,
            )
            mbedtls_objs.append(str(obj))

        exe_path = WORK / "test_native_updater.exe"
        app_srcs = [
            ROOT / "xdk/tests/test_native_updater.cpp",
            ROOT / "src/update/update_crypto.c",
            ROOT / "src/update/update_manifest.c",
            ROOT / "src/update/update_journal.c",
            ROOT / "src/update/update_http.c",
            ROOT / "src/update/update_service.c",
            ROOT / "src/update/update_tls_roots.c",
            ROOT / "third_party/monocypher/monocypher.c",
            ROOT / "third_party/monocypher/monocypher-ed25519.c",
        ]
        subprocess.run(
            [
                cl, "/nologo", "/O2", "/W3", "/EHsc", "/D_CRT_SECURE_NO_WARNINGS",
                "/DMOONX_UPDATE_HOST_TEST=1",
                "/I" + str(mbedtls_dir / "include"),
                *[("/Tp" + str(s)) for s in app_srcs],
                *mbedtls_objs,
                "ws2_32.lib", "advapi32.lib",
                "/Fe" + str(exe_path),
                "/Fo" + str(obj_dir) + "\\",
            ],
            env=env, check=True,
        )

        test_work = WORK / "runtime"
        test_work.mkdir(exist_ok=True)
        subprocess.run(
            [
                str(exe_path),
                str(test_work),
                pub_hex,
                base_url,
                str(ca_pem),
                str(srv_dir / "stable/manifest.json"),
                str(srv_dir / "stable/manifest.json.sig"),
                str(srv_dir / "beta/manifest.json"),
                str(srv_dir / "beta/manifest.json.sig"),
                str(p101),
                str(p102),
                str(bad_port),
            ],
            check=True,
        )
    finally:
        good_httpd.shutdown()
        bad_httpd.shutdown()


if __name__ == "__main__":
    main()
