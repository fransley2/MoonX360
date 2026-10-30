# Vendored dependencies and build boundaries

This publication contains curated dependency source snapshots, including local
Xbox 360 adaptations. The files shipped here, rather than an unverified remote
revision, are the build inputs. This document records a source inspection on
2026-10-30; see PUBLICATION.md for the completed clean-build checks. Binary reproducibility was not established.

## Versions and provenance

| Source directory under `xdk/deps/` | Version evidence | Project reference; provenance limit |
| --- | --- | --- |
| `mbedtls-2.16.12` | `include/mbedtls/version.h` reports 2.16.12 | [Mbed TLS](https://github.com/Mbed-TLS/mbedtls); the original build notes name the `mbedtls-2.16.12` archive. Archive identity and an upstream-equality comparison were not verified. |
| `opus-1.5.2` | `package_version` reports 1.5.2 | [Opus](https://opus-codec.org/), [release archive](https://downloads.xiph.org/releases/opus/opus-1.5.2.tar.gz). Local C89 adaptations are retained; archive equality was not verified. |
| `moonlight-common-c` | No authenticated revision metadata in this snapshot | [Moonlight common C](https://github.com/moonlight-stream/moonlight-common-c). Earlier build notes name abbreviated revision `4a95eb35`; this is a historical reference, not a verified pin for these files. Includes ENet and nanors source copies. |
| `XBMC-360/libraries/ffmpeg` | `RELEASE` reports 1.2.12 | [XBMC-360](https://github.com/brentdc-nz/XBMC-360), FFmpeg port. Earlier build notes name abbreviated revision `20658be9`; equality to that revision was not verified. This is an adapted FFmpeg subtree, not an untouched FFmpeg release. |

Dependency Git histories are absent. In the original working tree, ENet and nanors
had `.git` pointer files whose parent module metadata was missing; those pointers
are not a recoverable provenance record. Running Git inside a dependency can
resolve the outer project repository and must not be interpreted as dependency
history. No exact dependency commit or clean Git status is asserted here.

## Retained source required by the dashboard build

- Mbed TLS: `include/`, `library/`, `LICENSE`, `apache-2.0.txt`, and `gpl-2.0.txt`.
  `xdk/build.py` compiles `library/*.c` with `xdk/mbedtls_config_xdk.h`.
- Opus: `src/`, `celt/`, `silk/`, `include/`, the root `*_sources.mk` and
  `*_headers.mk` files, version information, and `COPYING`/`AUTHORS`. This curated
  snapshot also retains `dnn/`. The dashboard builder reads selected source lists
  from `celt_sources.mk`, `silk_sources.mk`, and `opus_sources.mk`; it excludes
  `opus_encoder.c`. Retained upstream build files do not imply that every upstream
  configure, test, or packaging target is supported by this reduced snapshot.
- Moonlight: `src/`, ENet sources and headers, and nanors sources, headers, and
  `nanors/deps/obl/`. The build compiles Moonlight except `PlatformCrypto.c`,
  selected ENet units, `nanors/rs.c`, `oblas_common.c`, and `oblas_lite.c`.
  `xdk/src/PlatformCrypto_xdk.c` and `xdk/src/enet_xbox.c` supply platform adapters.
- FFmpeg: `libavcodec/`, `libavutil/`, `vcproj/`, root `config.h`, version files,
  and all retained license and attribution files. Keep the three project source
  lists at `vcproj/libavcodec/libavcodec.vcxproj`,
  `vcproj/libavutil/libavutil.vcxproj`, and `vcproj/pthreads/pthreads.vcxproj`.
  They enumerate 463, 45, and one C translation unit respectively; the pthread
  translation unit includes many other C files. Keep `vcproj/external/pthreads/`,
  `vcproj/external/include/`, and required zlib headers. Do not reduce pthreads to
  `pthread.c` alone. The source-list paths existed at inspection time.

The FFmpeg root configuration reports LGPL 2.1 or later, `CONFIG_GPL=0`,
`CONFIG_GPLV3=0`, `CONFIG_VERSION3=0`, and `CONFIG_NONFREE=0`. Configuration labels
do not override individual source notices. The retained source is broader than
the code actually linked; its notices must also be preserved.

## Local changes are part of the source release

Retain the current dependency bytes and the project build scripts together.
`xdk/patches/moonlight-common-c.patch` covers C++ compatibility, endianness, and
socket changes, but is not a complete reconstruction recipe for the current
snapshot: `xdk/build.py` also inserts a thread-configuration call into
Moonlight's `src/Platform.c`. Do not blindly reapply the patch to vendored files.

`xdk/build.py` also applies an idempotent C89 declaration fix in Opus
`src/repacketizer.c`. `xdk/scripts/build_ffmpeg.py` inserts Xbox DSP hooks in
`h264qpel`, `h264chroma`, `h264dsp`, and `videodsp` C/header files, compiles
`xdk/ffmpeg_xbox/h264_vmx.c`, and uses `patch_worker_map.py` and
`slice_dispatch.py` for worker layout and optional generated dispatch code.
Those scripts are required even when the vendor files already contain hooks.
No complete upstream-to-local diff was established during this audit; vendoring
the adapted sources avoids making the old patch a false completeness claim.

## Proprietary-file exclusion and inspection result

The original pthreads subtree contained `vcproj/external/pthreads/need_errno.h`,
an apparent Microsoft CRT header copy with a 1985-1997 Microsoft copyright and
an all-rights-reserved notice. No redistribution grant was established for it.
It must be excluded from this publication. Its `[Public]` comment is not a license.

References in `pthread.h`, `sched.h`, and `semaphore.h` are guarded by
`NEED_ERRNO`. The supplied `config.h` undefines that macro by default and defines
it for `WINCE`; the dashboard compiler flags do not select `WINCE`. Thus the
dashboard configuration uses the installed runtime's `<errno.h>`. Removing the
fallback does not preserve Windows CE support. The clean dashboard build after exclusion passed; see PUBLICATION.md.

The inspected `xdk/compat/include/` headers are small adapters for missing CRT,
Win32, and Winsock interfaces. They reference installed SDK headers such as
`xtl.h` and `ppcintrinsics.h`; no copied Microsoft copyright block was found
there. That observation is not independent proof of authorship or clean-room
development. Preserve upstream project licensing and attribution.

Microsoft mentions in Opus patent references, GPL explanatory text, platform
README text, and pthreads `version.rc` examples are not, by themselves, proprietary
ownership notices. The latter has a pthreads contributor/LGPL file notice.
FFmpeg's `dxva2api.h` has an explicit LGPL notice; its API-like name alone is not
evidence of an SDK copy. No additional Microsoft-owned source copy was identified
in the inspected files. This is a scoped source review, not a rights warranty.

## Building without publishing the SDK

An independently obtained Xbox 360 SDK is still necessary. Set `XEDK` to an
external installation with the Jul12 compiler, SDK headers/libraries, `spac.exe`,
and `imagexex.exe`. The original build notes identify SDK 21256.3. Do not include
the SDK's executables, headers, libraries, CRT source, or extracted tools here.
Project XML resource descriptions and scripts that invoke installed tools are
separate from redistribution of those tools.

From this publication's root, using a fresh output directory:

```powershell
$env:XEDK = 'C:\path\to\your\XDK'
$env:XDK_BUILD_DIR = Join-Path $PWD 'xdk\build\clean'
$env:XDK_OPTIMIZATION = 'aggressive'
$env:XDK_WORKER_MAP = 'current'
$env:XDK_SLICE_DISPATCH = 'legacy'
python xdk\scripts\build_ffmpeg.py
python xdk\build.py
```

Set the same profile/layout/dispatch settings for both build steps. Select
`compatible` or `batched` explicitly if that is the release being reproduced.
The scripts can modify vendored source as described above; build in a disposable
copy when preserving a release snapshot. The root Makefile and
`scripts/build-deps.sh` are a separate libxenon/XeLL path with different dependency
versions and an unpinned Moonlight `master`; they do not reproduce the XDK build.

Before publication, hash the final vendored inputs, record tool versions and
build options, finish a clean build from this curated tree, and publish matching
source with any distributed binary. A source-complete build is not a claim of
bit-for-bit reproducibility or physical-console validation. GPL Corresponding
Source includes necessary build scripts; any System Library exclusion must be
assessed under the actual license terms rather than assumed from an SDK name.
See [GPLv3](https://www.gnu.org/licenses/gpl.en.html) and
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).
