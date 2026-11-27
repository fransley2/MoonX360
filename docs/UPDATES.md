# MoonX360 Native Auto-Update System

MoonX360 (`v0.2.6+`) includes a native, cryptographically verified update system designed specifically for Xbox 360 FATX storage and controller-first navigation.

## Architecture Overview

MoonX360 distributes as a single user-facing executable (`default.xex`) while separating the permanent launcher from the internally managed application runtime so a running executable never overwrites itself:

- `default.xex` — Permanent lightweight XEX launcher (Title ID `0x4D583336`, independent of FFmpeg/D3D9) with embedded verified `moonx360.xex` payload (`MOONXAPP`). Extracts and verifies the runtime on first launch without re-extracting on subsequent boots.
- `updates/runtime/moonx360.xex` — Internally managed MoonX360 application executable (hidden inside `updates/runtime/` so Aurora does not create duplicate library entries).
- `updates/runtime/moonx360.next.bin` — Verified update candidate waiting to be promoted by the launcher.
- `updates/runtime/moonx360.previous.bin` — Last known-good executable preserved for automatic or manual rollback.
- `updates/runtime/moonx360.failed.bin` — Unconfirmed or failed trial candidate kept aside until the next download.
- `updates/` — Signed manifests, installation journal (`journal.json` / `journal.json.bak`), `launcher.log`, and `update.log`.
- `moonlight/` — Your existing user data (`moonlight.ini`, `hosts.tsv`, `client.pem`, `key.pem`, profiles, favorites, custom wallpapers, Aurora shortcuts index). **Never overwritten by updates.**

---

## Updating Inside MoonX360

1. Open **Settings → General → App Updates**.
2. MoonX360 automatically checks for updates after a healthy startup (when **Automatic update check** is **On**) and every 4 hours while idle on Home/Settings. It never checks or downloads while a streaming session is active.
3. When an update is available, a compact non-intrusive badge appears on **Home**.
4. In **App Updates**, review the installed version, available version, release notes, and download size, then press **A** on **Download update** and confirm.
5. While downloading:
   - Progress percentage and download speed are displayed live.
   - Press **B** at any time to cancel safely (partial temporary files are deleted).
6. Once verified (XEX2 header + payload size + SHA-256 + Ed25519 signature), select **Restart now** to reopen `default.xex` and apply the update, or **Install later** to apply it on your next launch.

> [!NOTE]
> Internet access is **not** required to open or use MoonX360. If the console is offline or GitHub is unreachable, MoonX360 continues operating normally.

---

## Release Channels: Stable vs. Beta

- **Stable** *(Default)* — Recommended releases published to `updates/stable/manifest.json`.
- **Beta** *(Explicit opt-in)* — Pre-release and testing builds published to `updates/beta/manifest.json`. Switching to Beta requires explicit confirmation.

Because MoonX360 tracks a monotonic `highest_build` high-water mark to block remote downgrade and replay attacks:

- Switching from **Stable** to **Beta** offers newer Beta builds (`build_id > highest_build`).
- Switching from **Beta** back to **Stable** blocks remote downgrades to older build IDs (`You're up to date.`) until a newer Stable build is published. If you still want to return to your local prior Stable executable immediately, use **Restore previous version**.

---

## One-Time Migration from Legacy Releases (`v0.2.5` and earlier)

Legacy releases used `default.xex` directly as the monolithic application without the permanent launcher. To migrate once:

1. Close MoonX360.
2. Back up your existing installation folder (especially `moonlight/`).
3. Extract the new release ZIP and copy the single-file **`default.xex`**, along with `MoonX360Shortcut/` and `AuroraScripts/` if you use Aurora Direct Launch, into your MoonX360 folder (for example `Hdd1:\Apps\MoonX360\` or `Usb0:\Apps\MoonX360\`). Delete any legacy loose `moonx360.xex` from the root directory if present.
4. Keep your existing `moonlight/` directory untouched.
5. Always launch **`default.xex`**.

Your existing **Aurora Direct Launch** shortcuts (`MoonX360Shortcut`) continue pointing to `default.xex`: the permanent launcher preserves `XGetLaunchData` / `XSetLaunchData` across the handoff to the internal `updates/runtime/moonx360.xex` runtime.

After this one-time manual installation, future compatible releases update only `updates/runtime/moonx360.xex` directly inside MoonX360. If a future update requires a newer permanent launcher (`min_launcher_version > installed`), MoonX360 displays manual package installation instructions instead of attempting self-replacement.

---

## Security Model

- **Ed25519 Signature Verification**: Every manifest is authenticated over its exact raw bytes using Monocypher 4.0.3 (`crypto_ed25519_check`) against the embedded public key in `src/update/update_public_key.h`. Private keys are never embedded in the app or committed to Git.
- **SHA-256 Streaming Verification**: Both the downloader and launcher compute streaming SHA-256 over the payload via mbedTLS 2.16.12 without loading the full XEX into RAM.
- **Validated HTTPS + Host Allowlist**: Uses an isolated mbedTLS 2.16.12 context with `MBEDTLS_SSL_VERIFY_REQUIRED`, embedded root CAs (`src/update/update_tls_roots.c`), SNI hostname validation, bounded chunked `Range` transfers, and up to 5 manually revalidated redirects restricted exclusively to `raw.githubusercontent.com/fransley2/MoonX360/updates/`, `github.com/fransley2/MoonX360/releases/`, and GitHub release CDN hosts (`objects.githubusercontent.com`, `release-assets.githubusercontent.com`, `github-releases.githubusercontent.com`).
- **Anti-Rollback & Replay Protection**: Rejects unknown schemas, non-`moonx360` products, oversized payloads (> 64 MiB), non-`XEX2` headers, channel mismatches, and `build_id <= highest_build`.

---

## Automatic Rollback & Manual Recovery

### Automatic Rollback

1. Before swapping executables, the launcher re-verifies the staged manifest signature and `moonx360.next.xex` SHA-256, removes any stale `moonx360.previous.xex`, records `phase: "swapping"` in `updates/journal.json`, moves `moonx360.xex` to `moonx360.previous.xex`, moves `moonx360.next.xex` to `moonx360.xex`, and records `phase: "trial"`.
2. Before any settings save during a `trial` boot, `moonlight.ini` is backed up to `updates/moonlight.ini.pre-update`.
3. `moonx360.xex` confirms `phase: "good"` **only** after config loads, D3D9 graphics/UI/fonts initialize, Home renders >= 10 frames, controller input is active, running version + `build_id` + XEX SHA-256 match, and at least 3 seconds elapse cleanly (with no PC host or internet required).
4. If the new version exits or fails before confirming `good`, the next launch of `default.xex` automatically moves `moonx360.xex` to `moonx360.failed.xex`, restores `moonx360.previous.xex` to `moonx360.xex`, restores `updates/moonlight.ini.pre-update`, and notifies `"Previous version restored."`.

### Manual Recovery

- **In-App Rollback**: In **Settings → General → App Updates**, select **Restore previous version** (available whenever `moonx360.previous.xex` exists) and restart.
- **Controller Boot Recovery Combo**: Hold **LB + RB + Back** while launching `default.xex` to schedule a controlled rollback to `moonx360.previous.xex` before `moonx360.xex` starts.
- **Manual File Recovery**: Using XeXMenu, Aurora File Manager, or FTP, delete `moonx360.xex` (or rename it to `moonx360.failed.xex`) and rename `moonx360.previous.xex` to `moonx360.xex`, or copy `default.xex` + `moonx360.xex` from a release ZIP while preserving `moonlight/`.

## Known Limitations

- **FATX Filesystem**: Xbox 360 FATX does not guarantee POSIX `fsync`/atomic rename durability under sudden AC power loss. The journal persists state transitions before each rename so interruptions between steps can be resumed safely, while true power-loss survival depends on FATX cluster flush behavior.
- **Kernel Hangs**: A user-mode XEX launcher cannot recover from an Xbox 360 kernel/XAM hard hang without a console power cycle; once power-cycled and reopened, the launcher automatically restores the unconfirmed trial build.
- **Physical Console Validation**: Host integration tests validate cryptographic verification, TLS, chunked retries, interrupted swaps, and rollback; physical Xbox 360 end-to-end verification should be completed before publishing production releases.
