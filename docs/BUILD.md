# Build MoonX360 for Xbox 360

Use Windows, Git, Python 3, and a separately obtained Xbox 360 SDK 21256.3 with the Jul12 compiler. Use an SDK you are authorized to access under Microsoft's applicable developer licensing. Set **XEDK** to its root. The root needs **bin\win32**, **include\xbox**, **lib\xbox**, and **TechPreview\Jul12Compiler**.

Build from a clean source checkout. The adapted dependencies are under **xdk/deps**. Keep them together with the build scripts. See [dependency provenance](DEPENDENCIES.md) for the included sources and notices.

    $env:XEDK = 'C:\Program Files (x86)\Microsoft Xbox 360 SDK'
    $env:XDK_BUILD_DIR = Join-Path $PWD 'xdk\build'
    $env:XDK_CONFIGURATION = 'Release'
    $env:XDK_OPTIMIZATION = 'aggressive'

    python xdk\scripts\build_ffmpeg.py
    if ($LASTEXITCODE) { throw 'FFmpeg build failed' }
    python xdk\build.py mbedtls opus compat moonlight app xex
    if ($LASTEXITCODE) { throw 'MoonX360 build failed' }

The application is written to **XDK_BUILD_DIR\default.xex**. The builder checks the FFmpeg manifest and library hashes, links the program, embeds the dashboard resource, and verifies the extracted resource. The **shortcut_xex** target builds the separate [Aurora launcher](AURORA_DIRECT_LAUNCH.md).

Choose a fresh output directory to preserve an existing build. Some builders apply idempotent compatibility edits to their vendored inputs. Use the same environment and output directory for both build commands. The **compatible** optimization profile is available for both steps.

The root **Makefile** targets libxenon/XeLL. Microsoft SDK tools, headers, and libraries are external prerequisites.
