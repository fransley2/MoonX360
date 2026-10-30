# Building MoonX360

Run commands from the repository root. The dashboard build is `xdk/build.py`; the root `Makefile` builds the separate XeLL backend.

## Dashboard prerequisites

- Windows and Python 3 (Python 3.14 is used for this publication's checks); Git for development.
- A separately obtained Xbox 360 XDK. The existing scripts target SDK 21256.3 and its `TechPreview/Jul12Compiler` compiler (16.10). Other SDK versions are not validated here.
- Set `XEDK` explicitly to the SDK root containing `bin/win32`, `include/xbox`, `lib/xbox` and `TechPreview/Jul12Compiler`. The legacy fallback path in the scripts is not an installation instruction.
- The redistributable dependency source snapshots under `xdk/deps` are included. See [DEPENDENCIES.md](DEPENDENCIES.md) for versions and local adaptations. No dependency download or proprietary prebuilt library is needed from this repository.

```powershell
$env:XEDK = 'C:\path\to\external\Xbox360SDK'
$env:XDK_OPTIMIZATION = 'aggressive'
python xdk/scripts/build_ffmpeg.py
python xdk/build.py
```

Output: `xdk/build/default.xex`. Intermediate libraries, objects, generated fonts, logs, metadata, executable and dashboard resources stay under `xdk/build`. `aggressive` is the build-script default; `compatible` selects the original `/O2` policy. These names do not establish a measured hardware speed difference. Use a different `XDK_BUILD_DIR` when comparing policies and keep the same policy for FFmpeg and the application:

```powershell
$env:XDK_OPTIMIZATION = 'compatible'
$env:XDK_BUILD_DIR = Join-Path $PWD 'xdk/build/compatible'
python xdk/scripts/build_ffmpeg.py
python xdk/build.py
```

`XDK_WORKER_MAP` defaults to `current` and `XDK_SLICE_DISPATCH` to `legacy`; the `previous` and `batched` alternatives are developer comparison modes, not recommended performance upgrades. Changing these requires rebuilding FFmpeg and the application together. `XDK_FFMPEG_ROOT` can select a separate adapted FFmpeg source copy.

The scripts compile mbedTLS, Opus, Moonlight, compatibility code and application code from source. They apply specific compiler/thread/DSP adaptations to the included dependency working files; repeated application is intended to be safe. FFmpeg's selected Visual Studio project source lists drive its build. Do not substitute a recent upstream FFmpeg tree. If headers, profiles or generated policy change, use a fresh build directory. The final build checks library hashes, embeds the MoonX360 SPA after linking with `/XEX:NO`, converts with external `imagexex`, then compares the extracted resource bytes.

The Microsoft tools `cl`, `lib`, `link`, `spac` and `imagexex`, SDK headers and SDK libraries must come from your external SDK. Do not commit or redistribute them. A historical, unused `need_errno.h` Microsoft CRT copy from pthreads is deliberately omitted; the Xbox build uses the SDK errno declarations and does not define `NEED_ERRNO`.

## Assets

Inter font files, brand PNGs, canonical SVG and generated `src/ui/brand.h` are included with their notices. Normal compilation does not require rasterization tools. To intentionally regenerate branding, install Pillow and resvg-py in a development environment and run `python tools/build_brand.py`; those optional package versions are not pinned or verified by this publication. The script preserves the SVG. Shaders are embedded in the platform source and compiled through the external XDK.

## Host tests

Desktop MSVC tests use Visual Studio 2022 Community by default. Override `VS_VCVARS64` to point to another installation's `vcvars64.bat`. They simulate APIs/devices and cannot validate real Xbox performance, GPU execution, networking or controller behavior.

```powershell
python -m unittest discover -s xdk/tests -p 'test_*policy.py'
python -m unittest discover -s xdk/tests -p 'test_build_cache.py'
python xdk/tests/run_host_tests.py
python xdk/tests/run_remote_features.py
python xdk/tests/run_expansion_tests.py
python xdk/tests/test_slice_dispatch.py
```

On Linux with GCC/G++ and Python 3, `python3 xdk/tests/run_portable_tests.py` runs the portable subset. It is not an XDK or XeLL build. Test outputs are ignored. See [PUBLICATION.md](PUBLICATION.md) for the checks actually performed for this snapshot and any failures.

## Legacy XeLL/libxenon backend

This path is retained for development; it was not built or console-tested for this publication. It requires Linux/WSL, GCC/make/Git/wget and an external libxenon cross toolchain (the existing Makefile comments reference GCC 16). The toolchain revision is not pinned, and the dependency script currently follows Moonlight `master`; this path is therefore not fully reproducible.

```sh
git clone https://github.com/Free60Project/libxenon ~/x360/libxenon
cd ~/x360/libxenon/toolchain
PREFIX=~/x360/devkitxenon ./build-xenon-toolchain toolchain
PREFIX=~/x360/devkitxenon ./build-xenon-toolchain libs
cd /path/to/MoonX360
bash scripts/build-deps.sh
make
```

`scripts/build-deps.sh` fetches mbedTLS 3.6.2, Opus 1.5.2, FFmpeg 7.1 and Moonlight `master`, builds hwpthread, and patches the external libxenon NIC receive ring. `DEVKITXENON`, `DEPS` and `LIBXENON_SRC` can override its defaults; inspect the script before running it. These libraries differ from the dashboard snapshots. Result: root `xenon.elf` (copied from `moonlight.elf32`). Copy `xenon.elf` to the FAT32 USB root and boot XeLL; wired networking/controllers are required by this backend. Do not treat this unverified build path as a tested alternative to the dashboard application.
