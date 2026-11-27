"""Generate a versioned MoonX360 update manifest for a packaged moonx360.xex."""
import argparse
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import struct
import tempfile
from urllib.parse import urlsplit

REPO_ROOT = Path(__file__).resolve().parents[1]
ALLOWED_ASSET_PREFIX = "https://github.com/fransley2/MoonX360/releases/download/"


def public_asset_url(url: str) -> bool:
    if not url or any(ord(c) < 33 or ord(c) > 126 for c in url):
        return False
    if any(bad in url for bad in ("@", "\\", "#", "%", "/./", "/../")):
        return False
    parsed = urlsplit(url)
    if parsed.scheme != "https" or parsed.port is not None or parsed.query or parsed.fragment:
        return False
    return url.startswith(ALLOWED_ASSET_PREFIX) and len(url) > len(ALLOWED_ASSET_PREFIX)


def test_asset_url(url: str) -> bool:
    if not url or any(ord(c) < 33 or ord(c) > 126 for c in url):
        return False
    if any(bad in url for bad in ("@", "\\", "#", "%", "/./", "/../")):
        return False
    parsed = urlsplit(url)
    if parsed.scheme not in ("http", "https") or parsed.query or parsed.fragment:
        return False
    if parsed.hostname == "localhost":
        return True
    try:
        addr = ipaddress.IPv4Address(parsed.hostname or "")
    except ValueError:
        return False
    return addr.is_private or addr.is_loopback


def validate_xex2(xex_path: Path, expected_title_id: str = "4D583336") -> tuple[int, str]:
    raw = xex_path.read_bytes()
    if raw[:4] != b"XEX2":
        raise SystemExit(f"Not an XEX2 executable: {xex_path}")
    if not (4096 <= len(raw) <= 64 * 1024 * 1024):
        raise SystemExit(f"Payload size out of bounds (4 KiB..64 MiB): {len(raw)}")
    header_count = struct.unpack_from(">I", raw, 20)[0]
    headers = dict(struct.unpack_from(">II", raw, 24 + i * 8) for i in range(header_count))
    if 0x40006 not in headers:
        raise SystemExit(f"Missing Execution ID header in {xex_path}")
    tid = raw[headers[0x40006] + 12 : headers[0x40006] + 16].hex().upper()
    if tid != expected_title_id.upper():
        raise SystemExit(f"Unexpected Title ID {tid} in {xex_path}; expected {expected_title_id}")
    return len(raw), hashlib.sha256(raw).hexdigest()


def validate_note(text: str, lang: str) -> str:
    raw = text.encode("utf-8")
    if not (1 <= len(raw) <= 1200):
        raise SystemExit(f"Release note for {lang} must be 1..1200 UTF-8 bytes")
    for ch in text:
        code = ord(ch)
        if (code < 32 and ch != "\n") or code == 127:
            raise SystemExit(f"Disallowed control character in {lang} release note")
    return text


def atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as handle:
            handle.write(data)
        os.replace(tmp_name, path)
    finally:
        if os.path.exists(tmp_name):
            os.unlink(tmp_name)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("xex", type=Path, help="Path to final packaged moonx360.xex")
    parser.add_argument("--version-file", type=Path, default=REPO_ROOT / "version.json")
    parser.add_argument("--version", help="Override human-readable version")
    parser.add_argument("--build-id", type=int, help="Override monotonically increasing build_id")
    parser.add_argument("--channel", choices=("stable", "beta"), help="Release channel (stable or beta)")
    parser.add_argument("--min-launcher-version", type=int, help="Minimum compatible launcher version")
    parser.add_argument("--url", required=True, help="HTTPS download URL for the packaged moonx360.xex")
    parser.add_argument("--allow-test-url", action="store_true", help="Allow private/loopback test server URL")
    parser.add_argument("--notes-en", required=True, help="English release notes")
    parser.add_argument("--notes-pt-br", required=True, help="Brazilian Portuguese release notes")
    parser.add_argument("--notes-es", required=True, help="Spanish release notes")
    parser.add_argument("--output", type=Path, required=True, help="Output manifest.json path")
    args = parser.parse_args()

    meta = json.loads(args.version_file.read_text(encoding="utf-8"))
    product = meta["product"]
    version = args.version or str(meta["version"])
    build_id = args.build_id if args.build_id is not None else int(meta["build_id"])
    channel = args.channel or str(meta["channel"])
    min_launcher = (
        args.min_launcher_version
        if args.min_launcher_version is not None
        else int(meta["min_launcher_version"])
    )

    if product != "moonx360":
        raise SystemExit(f"Unsupported product in version file: {product}")
    if not re.fullmatch(r"[0-9A-Za-z.+-]{1,40}", version):
        raise SystemExit(f"Invalid version string: {version}")
    if not (1 <= build_id <= 2147483647):
        raise SystemExit(f"Invalid build_id: {build_id}")
    if channel not in ("stable", "beta"):
        raise SystemExit(f"Invalid channel: {channel}")
    if not (1 <= min_launcher <= 65535):
        raise SystemExit(f"Invalid min_launcher_version: {min_launcher}")
    if not public_asset_url(args.url) and not (args.allow_test_url and test_asset_url(args.url)):
        raise SystemExit("Asset URL is outside trusted GitHub release allowlist (use --allow-test-url only for local test kits)")

    payload_size, sha256_hex = validate_xex2(args.xex)
    manifest = {
        "schema": 1,
        "product": product,
        "version": version,
        "build_id": build_id,
        "channel": channel,
        "min_launcher_version": min_launcher,
        "payload_size": payload_size,
        "sha256": sha256_hex,
        "url": args.url,
        "notes": {
            "en": validate_note(args.notes_en, "en"),
            "pt-BR": validate_note(args.notes_pt_br, "pt-BR"),
            "es": validate_note(args.notes_es, "es"),
        },
    }
    encoded = (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    if len(encoded) > 65536:
        raise SystemExit("Manifest exceeds 64 KiB limit")
    atomic_write(args.output, encoded)
    print(f"Wrote {args.output} ({len(encoded)} bytes, build_id={build_id}, sha256={sha256_hex})")


if __name__ == "__main__":
    main()
