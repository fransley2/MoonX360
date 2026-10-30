# Source publication and validation

This source snapshot was prepared on 2026-10-30 from the existing MoonX360 working directory. At the initial inspection that directory had no Git metadata. The owner subsequently began initializing Git there in parallel. Publication therefore uses a separate curated repository; the original directory, its developing history and remotes are not changed or pushed. No upstream history is invented or attributed to the current owner. See [Xenonlight](https://github.com/roderick-neuhoff/Xenonlight) for the project's origin and upstream history.

## Scope

Included: application source and headers, platform compatibility layers, embedded shader source, canonical brand assets, redistributable Inter fonts, stb, relevant regression fixtures and test generators, dashboard project metadata, build scripts, adapted dependency source, licenses and maintenance documentation.

Excluded from the source Git tree: XEX/ELF/executable output, libraries/objects and generated resources, build caches, local IDE state, logs, runtime host/settings/pairing data, private identities, historical evidence/checkpoints/reports and release archives. The initial source publication had no release; subsequent dashboard packages are distributed separately through GitHub Releases with matching source and notices. The historical packaging tool and snapshot-verification script depended on private old evidence/documents and are omitted. Source and fixture files are not removed merely for being binary: the licensed font and image assets remain.

The Microsoft CRT copy `need_errno.h` in the old pthreads source is excluded. The active Xbox configuration does not use that fallback. Microsoft XDK installers, tools, headers and libraries remain external. Project-authored compatibility wrappers which include external SDK APIs remain in the source.

Mbed TLS includes publicly distributed test identities in `library/certs.c`; these are upstream test data, never a user's console certificate or private pairing key. That file, and the parser/writer files containing PEM delimiter strings, were compared with the official `mbedtls-2.16.12` GitHub release tag and matched after newline normalization. They are retained as dependency source, with their existing license. Do not use test identities as real credentials. Other identity files and runtime data are excluded.

## Checks performed

- SHA-256 comparison found no changes to the inspected original files. Application/runtime source and assets in this publication match the original bytes. Test harness and documentation changes are separate from application behavior.
- Clean native build with the external XDK 21256.3 Jul12 tools, default `aggressive` / `current` / `legacy` policy: FFmpeg pthreads (1 unit), avutil (45), avcodec (464 including project VMX), mbedTLS (79), Opus (136), compatibility layer (3), Moonlight (32), application (24). No compilation failures. Output was a local `default.xex`; it is ignored and not published.
- The build's final XEX conversion and byte-for-byte embedded SPA extraction check passed.
- Python policy/cache tests passed (9 policy tests and 3 cache tests).
- The desktop MSVC host suite passed across its initial segment and remaining segment after updating the stale display fixture for the existing fourth RGBA shader. Its original fixture declared only three shader slots. No production shader or renderer source was changed.
- Remote-feature, expansion/menu/storage and slice-dispatch host suites passed. Remote/expansion runners now use ignored test output folders instead of historical evidence folders. These are simulations, not physical device/network validation.
- The staged publication was checked for generated files, secret patterns and oversized files; the public dependency test-data exceptions above were reviewed. License notices and selected compatibility headers were inspected for proprietary copies.

No physical Xbox/LAN test was performed for this publication. Stable frame rates, Guide behavior on hardware, surround output, multicast discovery, remote input, Wi-Fi and recovery under real load remain hardware validation work. The XeLL toolchain was not compiled. No bit-for-bit reproducible-build claim or universal absence-of-secrets guarantee is made by these bounded inspections.

## Maintenance

Builds can apply idempotent adaptations inside dependency source directories. Check `git diff` after a build and retain necessary source changes. Preserve per-file notices. Before publishing a binary, supply matching source and required notices and review the applicable distribution terms. Keep generated artifacts and all console runtime data outside Git.
