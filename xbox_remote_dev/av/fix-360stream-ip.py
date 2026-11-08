"""Patch only LAN selection order in the user's audited, non-redistributed viewer."""
import argparse
import ast
import hashlib
import os
from pathlib import Path
import socket
from types import SimpleNamespace

ORIGINAL = "7be4cf4c559b698480d5192591cf97463e89e88dbb1d6efe02bcef41a937a48b"
PATCHED = "f5530813079b892217a1c39469d1ac4a8ca562964fbcd1bf66b6954f6be669ab"


def validate(source):
    tree = ast.parse(source)
    fn = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "detect_local_ipv4")
    code = compile(ast.Module(body=[fn], type_ignores=[]), "viewer-ip-regression", "exec")
    fixtures = [(["172.16.0.2", "192.168.15.8"], "192.168.15.8"),
                (["172.16.0.2", "10.0.0.8"], "10.0.0.8"),
                (["192.168.15.8", "172.16.0.2"], "192.168.15.8"),
                (["172.20.0.8"], "172.20.0.8"),
                (["127.0.0.1"], "Unknown - run ipconfig")]
    for ips, expected in fixtures:
        fake = SimpleNamespace(AF_INET=socket.AF_INET, SOCK_STREAM=socket.SOCK_STREAM,
                               gethostname=lambda: "fixture-host",
                               getaddrinfo=lambda *args: [(socket.AF_INET, socket.SOCK_STREAM, 0, "", (ip, 0)) for ip in ips],
                               gethostbyname_ex=lambda *args: ("fixture-host", [], ips))
        namespace = {"socket": fake}
        exec(code, namespace)
        assert namespace["detect_local_ipv4"]() == expected, (ips, expected)
    return len(fixtures)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("viewer", nargs="?", type=Path,
                        default=Path(__file__).resolve().parents[1] / "360Stream/PC/PC-PERFORMER-AV-v38.py")
    args = parser.parse_args()
    data = args.viewer.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    if sha not in (ORIGINAL, PATCHED):
        raise ValueError("Viewer differs from both audited versions; refusing patch")
    if sha == ORIGINAL:
        start = data.index(b"def detect_local_ipv4():")
        end = data.index(b"def timestamp():", start)
        needle = b'        parts = ip.split(".")'
        assert data[start:end].count(needle) == 1
        newline = b"\r\n" if b"\r\n" in data else b"\n"
        replacement = (b"    # Prefer physical LAN candidates before 172.16/12 VPN adapters." + newline +
                       b"    for ip in candidates:" + newline + needle)
        patched = data[:start] + data[start:end].replace(needle, replacement, 1) + data[end:]
        assert hashlib.sha256(patched).hexdigest() == PATCHED
        validate(patched)
        backup = Path(os.environ["LOCALAPPDATA"]) / "XboxRemoteDev/360stream-viewer/PC-PERFORMER-AV-v38.original.py"
        backup.parent.mkdir(parents=True, exist_ok=True)
        if backup.exists():
            assert backup.read_bytes() == data, "existing backup differs"
        else:
            backup.write_bytes(data)
        args.viewer.write_bytes(patched)
        data = patched
    print("IP regression fixtures passed:", validate(data))
    print("Viewer SHA-256:", hashlib.sha256(data).hexdigest())


if __name__ == "__main__":
    main()
