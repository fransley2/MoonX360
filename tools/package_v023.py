"""Package the v0.2.3 dashboard as one copyable MoonX360 folder."""

import argparse
import hashlib
import json
import shutil
import struct
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERSION = "v0.2.3"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_xex(path, title_id):
    data = path.read_bytes()
    assert data[:4] == b"XEX2", path
    count = int.from_bytes(data[20:24], "big")
    headers = {
        struct.unpack_from(">II", data, 24 + i * 8)[0]:
        struct.unpack_from(">II", data, 24 + i * 8)[1]
        for i in range(count)
    }
    assert 0x103FF in headers and 0x2FF in headers and 0x40006 in headers
    assert data[headers[0x40006] + 12:headers[0x40006] + 16] == bytes.fromhex(title_id)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--xex", type=Path, required=True)
    ap.add_argument("--shortcut-xex", type=Path, required=True)
    ap.add_argument("--source-commit", required=True)
    ap.add_argument("--user-tested-hash", default="")
    args = ap.parse_args()
    check_xex(args.xex, "4D583336")
    check_xex(args.shortcut_xex, "4D583353")
    out = ROOT / "dist"
    folder = out / "MoonX360"
    assert folder.resolve().parent == out.resolve()
    if folder.exists():
        shutil.rmtree(folder)
    (folder / "MoonX360Shortcut").mkdir(parents=True)
    (folder / "licenses").mkdir()
    shutil.copy2(args.xex, folder / "default.xex")
    shutil.copy2(args.shortcut_xex, folder / "MoonX360Shortcut" / "default.xex")
    shutil.copy2(ROOT / "INSTALL_v0.2.3.txt", folder / "LEIA-ME.txt")
    licenses = {
        "MoonX360-GPL-3.0.txt": "LICENSE",
        "Moonlight.txt": "xdk/deps/moonlight-common-c/LICENSE.txt",
        "mbedTLS.txt": "xdk/deps/mbedtls-2.16.12/apache-2.0.txt",
        "Opus.txt": "xdk/deps/opus-1.5.2/COPYING",
        "FFmpeg-LGPL.txt": "xdk/deps/XBMC-360/libraries/ffmpeg/COPYING.LGPLv2.1",
        "FFmpeg-GPL.txt": "xdk/deps/XBMC-360/libraries/ffmpeg/COPYING.GPLv2",
        "ENet.txt": "xdk/deps/moonlight-common-c/enet/LICENSE",
        "nanors.txt": "xdk/deps/moonlight-common-c/nanors/LICENSE",
        "Inter.txt": "assets/fonts/Inter-LICENSE.txt",
    }
    for name, source in licenses.items():
        shutil.copy2(ROOT / source, folder / "licenses" / name)
    stb = (ROOT / "third_party/stb/stb_truetype.h").read_text(encoding="utf-8")
    (folder / "licenses/stb.txt").write_text(
        stb[stb.rindex("This software is available under 2 licenses"):], encoding="utf-8"
    )
    info = {
        "version": VERSION,
        "source_commit": args.source_commit,
        "xex_sha256": digest(folder / "default.xex"),
        "shortcut_xex_sha256": digest(folder / "MoonX360Shortcut/default.xex"),
        "user_reported_tested_xex_sha256": args.user_tested_hash or None,
        "physical_validation_of_packaged_xex_by_codex": False,
    }
    (folder / "BUILD_INFO.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    files = sorted(p for p in folder.rglob("*") if p.is_file())
    (folder / "SHA256SUMS.txt").write_text(
        "".join(f"{digest(p)}  {p.relative_to(folder).as_posix()}\n" for p in files), encoding="ascii"
    )
    archive = out / f"MoonX360-{VERSION}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for p in sorted(p for p in folder.rglob("*") if p.is_file()):
            z.write(p, p.relative_to(out))
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        assert all(n.startswith("MoonX360/") for n in z.namelist())
    print(f"{archive}\nSHA-256 {digest(archive)}\nXEX SHA-256 {info['xex_sha256']}")


if __name__ == "__main__":
    main()
