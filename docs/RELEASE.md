# MoonX360 Release & Signing Workflow

This guide documents how maintainers build, verify, sign, and publish MoonX360 releases using the canonical `version.json` metadata and Ed25519 update manifests.

## 1. Key Management (One-Time Setup)

Store the production Ed25519 private signing key **outside** the Git repository (for example `%USERPROFILE%\.config\moonx360\update-ed25519.pem`):

```powershell
python tools/generate_update_key.py --key "$env:USERPROFILE\.config\moonx360\update-ed25519.pem" --header src/update/update_public_key.h
```

- **Never** commit `.pem` private keys, upload them to GitHub, or bundle them in packages.
- **Never** use disposable `.work/` test keys for production releases.
- `generate_update_manifest.py` and `sign_update_manifest.py` always require an explicit `--key` path.

## 2. Bump Canonical Version Metadata

Edit `version.json` at the repository root (the single source of truth shared by `xdk/build.py`, `src/version_info.h`, `tools/generate_update_manifest.py`, and `tools/package_release.py`):

- `version`: Human-readable semver string (e.g. `"0.2.6"`), kept in sync with `src/version_info.h`.
- `build_id`: Strictly increasing positive integer (`100`, `101`, ...). **Never reuse a rejected or rolled-back `build_id`.**
- `launcher_version` / `min_launcher_version`: Integer launcher ABI version (`1` unless launcher behavior changes).
- `channel`: `"stable"` or `"beta"`.

## 3. Build Production Binaries

Ensure `MOONX_UPDATE_TEST_CONFIG` and `MOONX_UPDATE_TEST_FAIL_BOOT` are **unset**, then build both the permanent launcher (`default.xex`) and main application (`moonx360.xex`) using the verified CPU release recipe:

```powershell
$env:XEDK = 'C:\Program Files (x86)\Microsoft Xbox 360 SDK'
python xdk/scripts/build_best_performance.py --out xdk/build_release_v026
python xdk/build.py launcher shortcut_xex
```

Verify outputs exist:
- `xdk/build_release_v026/default.xex` (single-file permanent launcher with embedded `MOONXAPP`)
- `xdk/build_release_v026/moonx360.xex` (standalone OTA application payload)
- `xdk/build_release_v026/MoonX360Shortcut/default.xex`

## 4. Package, Generate & Sign Channel Manifest

Run `tools/package_release.py` with a passing validation report and explicit `--sign-key`:

```powershell
python tools/package_release.py `
  --build xdk/build_release_v026 `
  --version v0.2.6 `
  --revision HEAD `
  --validation docs/VALIDATION_v0.2.6.json `
  --channel stable `
  --sign-key "$env:USERPROFILE\.config\moonx360\update-ed25519.pem" `
  --notes-en "Release summary in English." `
  --notes-pt-br "Resumo da versao em Portugues do Brasil." `
  --notes-es "Resumen de la version en Espanol."
```

This verifies:
- `XEX2` headers, Title IDs (`4D583336` for `default.xex` and `moonx360.xex`, `4D583353` for shortcuts), embedded SPAs, `MOONXAPP` payload match, `imagexex /dump` imports, and zero test markers.
- `SHA256SUMS.txt` inside `dist/MoonX360-v0.2.6.zip`.
- Standalone `dist/MoonX360-v0.2.6-moonx360.xex` matches the exact SHA-256 recorded in `dist/updates/stable/manifest.json`.
- Ed25519 signature `dist/updates/stable/manifest.json.sig` verifies against the exact bytes of `manifest.json`.

## 5. Publish in Safe Order

1. **Publish the GitHub Release first** (`https://github.com/fransley2/MoonX360/releases/tag/v0.2.6`) with:
   - `default.xex` (single-file installation & permanent launcher)
   - `MoonX360-v0.2.6.zip` (full install & one-time migration package)
   - `moonx360.xex` (renamed from `MoonX360-v0.2.6-moonx360.xex` to match the URL in `manifest.json`)
   - `MoonX360-v0.2.6-sources.zip`
   - `SHA256SUMS.txt`
2. **Verify the release asset URL returns HTTP 200/302** before offering the update.
3. **Commit and push `manifest.json` and `manifest.json.sig` together** to the `updates` branch under:
   - `stable/manifest.json` + `stable/manifest.json.sig` (for Stable), or
   - `beta/manifest.json` + `beta/manifest.json.sig` (for Beta).
   Never reformat `manifest.json` after signing!

## 6. Local Test Kit (No GitHub Publication)

To validate updates on a physical Xbox 360 against a local LAN server with disposable keys:

```powershell
python tools/build_update_test_kit.py --base-url http://<PC-LAN-IP>:8266/
python .work/update-test-kit/serve_test_updates.py 8266
```
