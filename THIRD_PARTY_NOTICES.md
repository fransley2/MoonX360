# Third-party notices

MoonX360 incorporates and adapts third-party source. Existing copyright notices
and license texts remain authoritative. This inventory identifies the source
copies inspected on 2026-10-30; it does not assert authenticated upstream commits
or independent authorship of local adaptations. See
[dependency provenance and build boundaries](docs/DEPENDENCIES.md).

| Component | License evidence retained in this publication |
| --- | --- |
| MoonX360 / Xenonlight-derived application and platform code | [Root GPLv3 text](LICENSE). Preserve upstream notices in shared code; renaming the application does not remove attribution. |
| Moonlight common C | [GPLv3 text](xdk/deps/moonlight-common-c/LICENSE.txt); [upstream project](https://github.com/moonlight-stream/moonlight-common-c). No exact revision verified. |
| ENet | MIT; Copyright (c) 2002-2020 Lee Salzman. [Full notice](xdk/deps/moonlight-common-c/enet/LICENSE); [Moonlight ENet fork](https://github.com/cgutman/enet). |
| nanors, including its retained `deps/obl` sources | MIT; Copyright (c) 2021 Joseph Calderon. [Full notice](xdk/deps/moonlight-common-c/nanors/LICENSE); [project](https://github.com/sleepybishop/nanors). No separate license file was found inside `deps/obl`; preserve the enclosing license and do not infer a separate OBL revision or provenance. |
| Mbed TLS 2.16.12 | [License selector](xdk/deps/mbedtls-2.16.12/LICENSE): Apache-2.0 OR GPL-2.0-or-later unless a file says otherwise. Both [Apache 2.0](xdk/deps/mbedtls-2.16.12/apache-2.0.txt) and [GPLv2](xdk/deps/mbedtls-2.16.12/gpl-2.0.txt) are retained. |
| Opus 1.5.2 | BSD-style redistribution terms and patent references in [COPYING](xdk/deps/opus-1.5.2/COPYING), with [AUTHORS](xdk/deps/opus-1.5.2/AUTHORS) and per-file notices. Copyright holders include Xiph.Org, Skype Limited, Octasic, Jean-Marc Valin, Timothy B. Terriberry, CSIRO, Gregory Maxwell, Mark Borgerding, Erik de Castro Lopo, Mozilla, and Amazon. |
| FFmpeg 1.2.12 as adapted by XBMC-360 | [License inventory](xdk/deps/XBMC-360/libraries/ffmpeg/LICENSE); [LGPL 2.1](xdk/deps/XBMC-360/libraries/ffmpeg/COPYING.LGPLv2.1), [LGPLv3](xdk/deps/XBMC-360/libraries/ffmpeg/COPYING.LGPLv3), [GPLv2](xdk/deps/XBMC-360/libraries/ffmpeg/COPYING.GPLv2), and [GPLv3](xdk/deps/XBMC-360/libraries/ffmpeg/COPYING.GPLv3). Most files are LGPL-2.1-or-later; some retained files have other or GPL terms. Keep per-file notices and [CREDITS](xdk/deps/XBMC-360/libraries/ffmpeg/CREDITS). |
| pthreads-win32 / Xbox adaptations within FFmpeg | [COPYING](xdk/deps/XBMC-360/libraries/ffmpeg/vcproj/external/pthreads/COPYING) describes LGPL 2.1; source headers also contain LGPL version 2-or-later wording. Preserve these notices and [CONTRIBUTORS](xdk/deps/XBMC-360/libraries/ffmpeg/vcproj/external/pthreads/CONTRIBUTORS). Copyrights include John E. Bossom, pthreads-win32 contributors, and Ross P. Johnson for the COPYING document. The LGPL 2.1 full text is retained at FFmpeg's root. |
| zlib 1.2.5 retained under FFmpeg `vcproj/external/zlib` | zlib license; Jean-loup Gailly and Mark Adler. Preserve [README](xdk/deps/XBMC-360/libraries/ffmpeg/vcproj/external/zlib/README), [zlib.h](xdk/deps/XBMC-360/libraries/ffmpeg/vcproj/external/zlib/zlib.h), and file notices. The unrelated contrib examples and their binary/help fixtures are excluded from publication. |
| MSVC-compatible `stdint.h` and `inttypes.h` in FFmpeg's external includes | Alexander Chemeris, 2006-2008 / 2006. Explicit permissive redistribution conditions are embedded in [stdint.h](xdk/deps/XBMC-360/libraries/ffmpeg/vcproj/external/include/stdint.h) and [inttypes.h](xdk/deps/XBMC-360/libraries/ffmpeg/vcproj/external/include/inttypes.h). Preserve the actual text rather than substituting a generic BSD notice. |
| `dxva2api.h` in FFmpeg's external includes | LGPL-2.1-or-later file notice; Geoffroy Couprie and Laurent Aimar. [Source notice](xdk/deps/XBMC-360/libraries/ffmpeg/vcproj/external/include/dxva2api.h). |
| stb image, image-write, and TrueType code | Each header offers MIT or public-domain/Unlicense alternatives; preserve its embedded notices: [stb_image.h](third_party/stb/stb_image.h), [stb_image_write.h](third_party/stb/stb_image_write.h), [stb_truetype.h](third_party/stb/stb_truetype.h). |
| Inter fonts | SIL Open Font License 1.1; Copyright (c) 2016 The Inter Project Authors. [Full notice and license](assets/fonts/Inter-LICENSE.txt). |

The tables are an index, not replacements for the linked license texts. License
terms in individual files, including ancillary source not linked into the
dashboard executable, continue to apply. Keep all retained third-party notices
with source distributions and provide the notices required by the selected
licenses with binary distributions. Modified third-party sources must remain
identified as modified; the Xbox adaptation scripts and source snapshot document
those changes without asserting an unverified original commit.

Microsoft's Xbox 360 SDK is an external proprietary build prerequisite and is
not licensed by this project. SDK headers, libraries, CRT copies, and tools must
not be bundled on the strength of the licenses above. The identified
`vcproj/external/pthreads/need_errno.h` Microsoft CRT fallback must be excluded;
the pthreads license does not establish permission to redistribute that file.
Microsoft names appearing in patent references or API documentation are not
themselves evidence that the containing open-source file is proprietary.

Providing dependency source and these notices does not alone settle the rights
to distribute an SDK-linked executable. Review the actual SDK terms and the
GPL's applicable linking/System Library provisions for that distribution.
See the [GNU GPLv3 text](https://www.gnu.org/licenses/gpl.en.html) and
[GNU license FAQ](https://www.gnu.org/licenses/gpl-faq.en.html).
