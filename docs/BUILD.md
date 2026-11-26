# Build MoonX360 from Source

MoonX360 compiles on Windows using **Python 3**, **Git**, and a separately obtained **Xbox 360 SDK 21256.3** (`TechPreview\Jul12Compiler`). Use an SDK you are authorized to access under Microsoft's applicable developer licensing.

Adapted third-party dependency sources (`FFmpeg 1.2.12`, `moonlight-common-c`, `Mbed TLS 2.16.12`, `Opus 1.5.2`) are included under `xdk/deps/`. See [DEPENDENCIES.md](DEPENDENCIES.md) for provenance and build boundaries.

---

## 1. Standard Clean Source Build

Set `XEDK` to your Xbox 360 SDK root (must contain `bin\win32`, `include\xbox`, `lib\xbox`, and `TechPreview\Jul12Compiler`):

```powershell
$env:XEDK = 'C:\Program Files (x86)\Microsoft Xbox 360 SDK'
$env:XDK_BUILD_DIR = Join-Path $PWD 'xdk\build\release'
$env:XDK_CONFIGURATION = 'Release'
$env:XDK_OPTIMIZATION = 'aggressive'

# Build FFmpeg with the v0.2.6-rc1 production flag set
$env:XDK_WORKER_MAP = 'current'
$env:XDK_SLICE_DISPATCH = 'legacy'
$env:XDK_LOW_LATENCY_INPUT = '1'
$env:XDK_INPUT_HW_THREAD = '0'
$env:XDK_YUV_CONTIGUOUS_PLANES = '1'
$env:XDK_WIRED_1392_MIN_KBPS = '4000'
$env:XDK_ENABLE_AUTO_LIVE_TEST = '0'
$env:XDK_H264_REFERENCE_MODE = '0'
$env:XDK_ADAPTIVE_FAST_DECODE = '0'

python xdk\scripts\build_ffmpeg.py
if ($LASTEXITCODE) { throw 'FFmpeg build failed' }
python xdk\build.py mbedtls opus compat moonlight app xex shortcut_xex
if ($LASTEXITCODE) { throw 'MoonX360 build failed' }
```

- Output executable: `xdk\build\release\default.xex`
- Optional Aurora direct-launch template: `xdk\build\release\MoonX360Shortcut\default.xex`

---

## 2. Production `v0.2.6-rc1` (`Final-release`) Patch Stack & Recipe

Unlike the `v0.2.5` `build_best_performance.py` recipe, `v0.2.6-rc1` (`Final-release`) builds with **`XDK_SLICE_DISPATCH=legacy`** (not `batched`), adds three `v026-rc2` H.264 loop-filter/chroma VMX patches on top of the 10 `batch16` patches, and enables `E5b + E7 + E8` compile-time optimizations:

- **13 FFmpeg patches applied (`xdk/patches/`):**
  - 10 `batch16` patches: `r47c-ordered-deblock`, `r47c-wavefront-deblock`, `r47c-row-progress`, `r47d-batch8`, `r47d-long-slices`, `r47d-edge16`, `r47d-edge-transpose`, `r47d-dispatch-profile`, `r47d-first-idr`, `r48-batch16`.
  - 3 `v026-rc2` patches: `v026-opt1-edge-nolhs`, `v026-opt4-chroma-lf-nolhs`, `v026-chroma-optA-vmx1d`.
- **Campaign reproduction script** (when reproducing alongside an existing `xdk/build_best_performance/default.xex` baseline):

```powershell
python xdk\scripts\build_v026_experiment.py --name Final-release --out xdk\build\v026-rc1-release --preset v026-rc2 --yuv-contiguous-planes --input-hw 0 --wired-1392-min-kbps 4000 --overwrite
```

- For clean checkouts without an existing `xdk/build_best_performance/` folder, apply the 13 patches above to a working copy of `xdk/deps/XBMC-360/libraries/ffmpeg` and run `build_ffmpeg.py` + `build.py` with the environment variables in §1.
- The root `Makefile` targets separate libxenon/XeLL builds and is not used for the XDK dashboard release.

---

## 3. Running Host Regression Tests (Windows + MSVC)

Requires Visual Studio C++ Build Tools / MSVC (`vcvars64.bat`):

```powershell
python xdk\tests\run_host_tests.py
python xdk\tests\run_expansion_tests.py
python xdk\tests\test_v026_stage0_and_exp1.py
python xdk\tests\run_vmx_mc_tests.py
```

These host suites run against simulated Xbox APIs on Windows to verify configuration, profiles, frame pacing, Smart Keyboard, Desktop input, and UI layout invariants. They complement, and never replace, physical Xbox 360 validation.
