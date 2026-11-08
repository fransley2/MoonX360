# Build MoonX360 for the Xbox 360 dashboard

MoonX360 v0.2.2 builds as default.xex for the Xbox 360 dashboard. Building requires Windows, Python 3, a separately obtained Xbox 360 SDK, and the project dependencies described in xdk/README.md. Microsoft SDK files are proprietary and are not included.

Set XEDK to the root of an installed or extracted SDK with bin\win32, include\xbox, lib\xbox, and TechPreview\Jul12Compiler. The build scripts use SDK 21256.3's Jul12 compiler.

From the repository root, prepare the dependencies listed in xdk/README.md and run:

    python xdk\scripts\build_ffmpeg.py
    python xdk\build.py

The production dashboard executable is written to xdk\build\default.xex. The build uses the aggressive optimization profile by default; XDK_OPTIMIZATION=compatible selects the compatible compiler profile. These names describe compiler flags, not measured console performance.

The build verifies the FFmpeg build manifest and library hashes, creates the dashboard resource, embeds it in the XEX, then extracts and compares the embedded resource. Keep SDK files and build output outside public source archives.
