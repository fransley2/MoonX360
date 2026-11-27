"""Sign or verify exact MoonX360 update manifest bytes using Ed25519."""
import argparse
import json
import os
from pathlib import Path
import tempfile

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ed25519

MAX_DOC_BYTES = 65536


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


def validate_manifest_bytes(raw: bytes) -> None:
    if not raw or len(raw) > MAX_DOC_BYTES:
        raise SystemExit("Manifest must be 1..65536 bytes")
    doc = json.loads(raw.decode("utf-8"))
    required = {
        "schema", "product", "version", "build_id", "channel",
        "min_launcher_version", "payload_size", "sha256", "url", "notes",
    }
    if set(doc.keys()) != required:
        raise SystemExit(f"Manifest keys mismatch: {sorted(doc.keys())}")
    if doc["schema"] != 1 or doc["product"] != "moonx360":
        raise SystemExit("Unsupported schema or product")
    if doc["channel"] not in ("stable", "beta"):
        raise SystemExit("Invalid channel")
    if set(doc["notes"].keys()) != {"en", "pt-BR", "es"}:
        raise SystemExit("Manifest notes must contain exactly en, pt-BR, es")


def load_private_key(path: Path) -> ed25519.Ed25519PrivateKey:
    key = serialization.load_pem_private_key(path.expanduser().read_bytes(), password=None)
    if not isinstance(key, ed25519.Ed25519PrivateKey):
        raise SystemExit("Key is not an Ed25519 private key")
    return key


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    p_sign = sub.add_parser("sign", help="Sign exact manifest bytes")
    p_sign.add_argument("manifest", type=Path, help="Path to manifest.json")
    p_sign.add_argument("--key", type=Path, required=True, help="Explicit path to Ed25519 private PEM key")
    p_sign.add_argument("--output", type=Path, help="Signature output path (default: <manifest>.sig)")

    p_ver = sub.add_parser("verify", help="Verify manifest.json + .sig")
    p_ver.add_argument("manifest", type=Path, help="Path to manifest.json")
    p_ver.add_argument("--sig", type=Path, help="Signature path (default: <manifest>.sig)")
    grp = p_ver.add_mutually_exclusive_group(required=True)
    grp.add_argument("--pub-hex", help="64-char Ed25519 public key hex")
    grp.add_argument("--key", type=Path, help="Private PEM key to derive public key from")

    args = parser.parse_args()
    raw = args.manifest.read_bytes()
    validate_manifest_bytes(raw)

    if args.command == "sign":
        priv = load_private_key(args.key)
        sig = priv.sign(raw)
        if len(sig) != 64:
            raise RuntimeError("Unexpected Ed25519 signature length")
        priv.public_key().verify(sig, raw)
        out_path = args.output or Path(str(args.manifest) + ".sig")
        atomic_write(out_path, (sig.hex() + "\n").encode("ascii"))
        print(f"Signed {args.manifest} -> {out_path}")
    else:
        sig_path = args.sig or Path(str(args.manifest) + ".sig")
        sig_text = sig_path.read_text(encoding="ascii").strip()
        if len(sig_text) != 128 or any(c not in "0123456789abcdef" for c in sig_text):
            raise SystemExit(f"Invalid signature format in {sig_path}")
        sig = bytes.fromhex(sig_text)
        if args.pub_hex:
            pub = ed25519.Ed25519PublicKey.from_public_bytes(bytes.fromhex(args.pub_hex))
        else:
            pub = load_private_key(args.key).public_key()
        try:
            pub.verify(sig, raw)
        except InvalidSignature as exc:
            raise SystemExit(f"Signature verification failed: {exc}") from exc
        print(f"Verified {args.manifest} with {sig_path}")


if __name__ == "__main__":
    main()
