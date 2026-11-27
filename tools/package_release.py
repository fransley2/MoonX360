"""Package a validated MoonX360 release (permanent launcher + main app + migration docs + optional signed manifest)."""
import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import zipfile

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_xex_binary(sdk: Path, source: Path, spa_path: Path, embedded_spa_path: Path,
                      title_id: str, require_xam: bool, allow_test_markers: bool) -> dict:
    data = source.read_bytes()
    assert data[:4] == b"XEX2", f"{source} is not XEX2"
    headers = dict(
        struct.unpack_from(">II", data, 24 + i * 8)
        for i in range(struct.unpack_from(">I", data, 20)[0])
    )
    assert 0x103FF in headers and 0x2FF in headers, f"Missing standard XEX headers in {source}"
    actual_tid = data[headers[0x40006] + 12 : headers[0x40006] + 16].hex().upper()
    assert actual_tid == title_id.upper(), f"Title ID mismatch in {source}: {actual_tid} != {title_id}"
    assert spa_path.read_bytes() == embedded_spa_path.read_bytes(), f"Embedded SPA mismatch for {source}"
    result = subprocess.run([str(sdk / "bin/win32/imagexex.exe"), "/dump", str(source)],
                            capture_output=True, check=True)
    dump = (result.stdout + result.stderr).decode(errors="replace")
    assert "xboxkrnl.exe" in dump, f"xboxkrnl.exe import missing in {source}"
    if require_xam:
        assert "xam.xex" in dump, f"xam.xex import missing in {source}"
    if not allow_test_markers:
        assert b"MOONX_UPDATE_TEST" not in data, f"Test update marker leaked into {source}"
    return {
        "sha256": sha(source),
        "bytes": len(data),
        "title_id": title_id,
        "xex2": True,
        "imports_checked": True,
        "embedded_spa_verified": True,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path, help="Build output directory (e.g. xdk/build_best_performance)")
    parser.add_argument("--version", help="Release tag (e.g. v0.2.7; defaults to v<version.json.version>)")
    parser.add_argument("--revision", default="HEAD", help="Git revision to package sources from")
    parser.add_argument("--validation", required=True, type=Path, help="Validation JSON report")
    parser.add_argument("--channel", choices=("stable", "beta"), help="Optional channel manifest output")
    parser.add_argument("--sign-key", type=Path, help="Explicit Ed25519 private key to sign manifest.json")
    parser.add_argument("--download-url", help="GitHub release asset URL for moonx360.xex")
    parser.add_argument("--notes-en", default="Native signed auto-update support and CPU streaming optimizations.")
    parser.add_argument("--notes-pt-br", default="Suporte nativo a atualizacoes assinadas e otimizacoes de streaming na CPU.")
    parser.add_argument("--notes-es", default="Soporte nativo de actualizaciones firmadas y optimizaciones de streaming en CPU.")
    parser.add_argument("--test-package", action="store_true", help="Explicitly allow test build artifacts")
    args = parser.parse_args()

    vmeta = json.loads((ROOT / "version.json").read_text(encoding="utf-8"))
    version_tag = args.version or ("v" + str(vmeta["version"]))
    assert re.fullmatch(r"v\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.]+)?", version_tag)
    if version_tag != "v" + str(vmeta["version"]) and not args.test_package:
        raise SystemExit(f"--version {version_tag} disagrees with version.json ({vmeta['version']})")

    revision = subprocess.check_output(["git", "rev-parse", args.revision], cwd=ROOT).decode().strip()
    validation = json.loads(args.validation.read_text(encoding="utf-8"))
    assert validation["passed"] and validation["failures"] == [], "Validation report did not pass"

    build = args.build.resolve()
    dist = ROOT / "dist"
    destination = dist / ("MoonX360-" + version_tag)
    assert not destination.exists(), f"Use a fresh package directory; already exists: {destination}"
    install = destination / "MoonX360"
    install.mkdir(parents=True)

    def copy(src: Path, name: str) -> None:
        target = install / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, target)

    sdk = Path(os.environ.get("XEDK", r"C:\Program Files (x86)\Microsoft Xbox 360 SDK"))
    binaries = []

    launcher_src = build / "default.xex"
    launcher_info = verify_xex_binary(
        sdk, launcher_src, build / "MoonX360.spa", build / "MoonX360Launcher-embedded.spa",
        "4D583336", require_xam=False, allow_test_markers=args.test_package,
    )
    copy(launcher_src, "default.xex")
    binaries.append({"path": "default.xex", "role": "permanent_launcher", **launcher_info})

    app_src = build / "moonx360.xex"
    app_info = verify_xex_binary(
        sdk, app_src, build / "MoonX360.spa", build / "MoonX360-embedded.spa",
        "4D583336", require_xam=True, allow_test_markers=args.test_package,
    )
    embedded_check = destination / "MoonX360Launcher-embedded-app.xex"
    subprocess.run(
        [str(sdk / "bin/win32/imagexex.exe"), "/dump", "/sectionname:MOONXAPP",
         "/sectionfile:" + str(embedded_check), str(launcher_src)],
        capture_output=True, check=True,
    )
    assert embedded_check.read_bytes() == app_src.read_bytes(), (
        "default.xex embedded MOONXAPP section does not match moonx360.xex"
    )
    embedded_check.unlink()
    launcher_info["embedded_app_sha256"] = app_info["sha256"]
    launcher_info["embedded_app_bytes"] = app_info["bytes"]
    binaries.append({"path": "standalone/moonx360.xex", "role": "ota_application_payload", **app_info})

    shortcut_src = build / "MoonX360Shortcut" / "default.xex"
    if shortcut_src.is_file():
        sc_folder = build / "MoonX360Shortcut"
        sc_info = verify_xex_binary(
            sdk, shortcut_src, sc_folder / "MoonX360Shortcut.spa", sc_folder / "MoonX360Shortcut-embedded.spa",
            "4D583353", require_xam=False, allow_test_markers=args.test_package,
        )
        copy(shortcut_src, "MoonX360Shortcut/default.xex")
        binaries.append({"path": "MoonX360Shortcut/default.xex", "role": "aurora_shortcut", **sc_info})

    wallpaper = ROOT / "assets/wallpapers/home-wallpaper.png"
    with Image.open(wallpaper) as im:
        assert im.format == "PNG" and 0 < im.width <= 2048 and 0 < im.height <= 2048
        dimensions = list(im.size)
        im.verify()
    assert wallpaper.stat().st_size <= 8 * 1024 * 1024
    copy(wallpaper, "Wallpapers/home-wallpaper.png")
    copy(ROOT / "assets/wallpapers/README.md", "Wallpapers/README.md")

    readme_src = ROOT / f"docs/RELEASE_NOTES_{version_tag}.md"
    if not readme_src.is_file():
        readme_src = ROOT / "README.md"
    copy(readme_src, "README.md")
    copy(ROOT / "THIRD_PARTY_NOTICES.md", "THIRD_PARTY_NOTICES.md")
    for name in ("UPDATES.md", "AURORA_DIRECT_LAUNCH.md", "ARTWORK_CACHE_AURORA_METADATA.md", "SETUP.md", "LIMITATIONS.md"):
        doc_path = ROOT / "docs" / name
        if doc_path.is_file():
            copy(doc_path, "docs/" + name)

    (install / "MIGRATION.txt").write_text(
        "MoonX360 Single-File Installation & Migration\n"
        "==============================================\n"
        "MoonX360 distributes as a single entry-point executable: default.xex.\n"
        "On first launch, default.xex extracts and SHA-256 verifies its internal\n"
        "application runtime into updates\\runtime\\moonx360.xex.\n\n"
        "1. Keep your existing moonlight\\ folder (moonlight.ini, hosts.tsv, client.pem, key.pem, profiles, wallpapers).\n"
        "2. Copy default.xex from this package over your MoonX360 installation folder\n"
        "   (along with AuroraScripts\\ and MoonX360Shortcut\\ if you use Aurora Direct Launch).\n"
        "3. Delete any legacy loose moonx360.xex from the root folder if present.\n"
        "4. Always launch default.xex. Subsequent OTA updates replace only updates\\runtime\\moonx360.xex.\n",
        encoding="utf-8",
    )

    licenses = {
        "MoonX360-GPL-3.0": "LICENSE",
        "Moonlight": "xdk/deps/moonlight-common-c/LICENSE.txt",
        "mbedTLS": "xdk/deps/mbedtls-2.16.12/apache-2.0.txt",
        "mbedTLS-selector": "xdk/deps/mbedtls-2.16.12/LICENSE",
        "mbedTLS-GPLv2": "xdk/deps/mbedtls-2.16.12/gpl-2.0.txt",
        "Monocypher": "third_party/monocypher/LICENCE.md",
        "Opus": "xdk/deps/opus-1.5.2/COPYING",
        "Opus-authors": "xdk/deps/opus-1.5.2/AUTHORS",
        "Inter": "assets/fonts/Inter-LICENSE.txt",
        "ENet": "xdk/deps/moonlight-common-c/enet/LICENSE",
        "nanors": "xdk/deps/moonlight-common-c/nanors/LICENSE",
    }
    ffroot = "xdk/deps/XBMC-360/libraries/ffmpeg/"
    licenses.update({name: ffroot + path for name, path in {
        "FFmpeg-LGPL": "COPYING.LGPLv2.1", "FFmpeg-GPL": "COPYING.GPLv2",
        "FFmpeg-GPLv3": "COPYING.GPLv3", "FFmpeg-LGPLv3": "COPYING.LGPLv3",
        "FFmpeg-inventory": "LICENSE", "FFmpeg-credits": "CREDITS",
        "pthreads": "vcproj/external/pthreads/COPYING",
        "pthreads-contributors": "vcproj/external/pthreads/CONTRIBUTORS",
        "zlib": "vcproj/external/zlib/README",
    }.items()})
    for name, path in sorted(licenses.items()):
        copy(ROOT / path, "licenses/" + name + ".txt")
    stb = (ROOT / "third_party/stb/stb_truetype.h").read_text(encoding="utf-8")
    (install / "licenses/stb.txt").write_text(
        stb[stb.rindex("This software is available under 2 licenses"):], encoding="utf-8"
    )

    for name in ("MoonX360Sync", "MoonX360Probe"):
        copy(ROOT / f"xdk/aurora/{name}/Main.lua", f"AuroraScripts/{name}/Main.lua")
        copy(ROOT / "assets/brand/MoonX360-64.png", f"AuroraScripts/{name}/icon.png")
    for image in sorted((ROOT / "assets/brand").glob("MoonX360*")):
        assert image.suffix in (".png", ".svg")
        copy(image, "icons/" + image.name)
    (install / "docs/VALIDATION.json").write_text(json.dumps(validation, indent=2) + "\n", encoding="utf-8")

    ffmpeg = json.loads((build / "ffmpeg/build-manifest.json").read_text(encoding="utf-8"))
    ffmpeg.pop("source_root", None)
    info = {
        "name": "MoonX360",
        "version": version_tag,
        "build_id": int(vmeta["build_id"]),
        "launcher_version": int(vmeta["launcher_version"]),
        "source_revision": revision,
        "release_date": datetime.now().strftime("%Y-%m-%d"),
        "test_only": bool(args.test_package),
        "profile": ffmpeg["profile"],
        "configuration": "Release",
        "ffmpeg": ffmpeg,
        "binaries": binaries,
        "wallpaper": {
            "path": "Wallpapers/home-wallpaper.png",
            "dimensions": dimensions,
            "sha256": sha(wallpaper),
        },
        "validation": "docs/VALIDATION.json",
    }
    (install / "BUILD_INFO.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")

    files = sorted(p for p in install.rglob("*") if p.is_file())
    allowed_root = {"default.xex", "README.md", "MIGRATION.txt", "THIRD_PARTY_NOTICES.md", "BUILD_INFO.json"}
    assert all(
        p.relative_to(install).as_posix() in allowed_root
        or p.relative_to(install).parts[0] in ("Wallpapers", "licenses", "docs", "icons", "AuroraScripts", "MoonX360Shortcut")
        for p in files
    )
    assert all(
        p.suffix.lower() not in (".ini", ".pem", ".key", ".pfx", ".crt", ".tsv", ".lib", ".obj", ".pdb", ".exe", ".dll")
        for p in files
    )
    (install / "SHA256SUMS.txt").write_text(
        "".join(sha(p) + "  " + p.relative_to(install).as_posix() + "\n" for p in files),
        encoding="ascii",
    )

    archive = dist / ("MoonX360-" + version_tag + ".zip")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for p in sorted(install.rglob("*")):
            if p.is_file():
                z.write(p, p.relative_to(destination))
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        for line in z.read("MoonX360/SHA256SUMS.txt").decode().splitlines():
            digest, name = line.split("  ", 1)
            assert hashlib.sha256(z.read("MoonX360/" + name)).hexdigest() == digest

    sources = dist / ("MoonX360-" + version_tag + "-sources.zip")
    subprocess.run(
        ["git", "archive", "--format=zip", "--prefix=MoonX360-source/", "--output=" + str(sources),
         revision, "--", ".", ":(exclude)xbox_remote_dev"],
        cwd=ROOT,
        check=True,
    )
    with zipfile.ZipFile(sources) as z:
        assert z.testzip() is None
        assert not any(
            Path(n).suffix.lower() in (".pem", ".key", ".pfx", ".crt", ".ini", ".tsv", ".exe", ".dll", ".lib", ".obj", ".xex")
            for n in z.namelist()
        )
        assert not any("/xbox_remote_dev/" in n or "/dev kits/" in n.lower() for n in z.namelist())

    standalone_app = dist / ("MoonX360-" + version_tag + "-moonx360.xex")
    shutil.copy2(app_src, standalone_app)
    checksums = dist / ("SHA256SUMS-" + version_tag + ".txt")
    checksums.write_text(
        "".join(sha(p) + "  " + p.name + "\n" for p in (archive, sources, standalone_app)),
        encoding="ascii",
    )

    if args.sign_key:
        ch = args.channel or str(vmeta["channel"])
        url = args.download_url or f"https://github.com/fransley2/MoonX360/releases/download/{version_tag}/moonx360.xex"
        manifest_out = dist / "updates" / ch / "manifest.json"
        subprocess.run(
            [
                __import__("sys").executable, str(ROOT / "tools/generate_update_manifest.py"),
                str(standalone_app),
                "--version", version_tag.lstrip("v"),
                "--build-id", str(vmeta["build_id"]),
                "--channel", ch,
                "--min-launcher-version", str(vmeta["min_launcher_version"]),
                "--url", url,
                "--notes-en", args.notes_en,
                "--notes-pt-br", args.notes_pt_br,
                "--notes-es", args.notes_es,
                "--output", str(manifest_out),
            ],
            check=True,
        )
        subprocess.run(
            [
                __import__("sys").executable, str(ROOT / "tools/sign_update_manifest.py"),
                "sign", str(manifest_out), "--key", str(args.sign_key),
            ],
            check=True,
        )

    print(json.dumps({
        "package": str(archive),
        "standalone_app": str(standalone_app),
        "sources": str(sources),
        "checksums": str(checksums),
        "launcher_sha256": launcher_info["sha256"],
        "app_sha256": app_info["sha256"],
        "files": len(files) + 1,
    }, indent=2))


if __name__ == "__main__":
    main()
