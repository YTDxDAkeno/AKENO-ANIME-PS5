# Third-party notices

## AKENO STREAM

AKENO STREAM is Copyright (C) 2026 AKENO STREAM contributors and licensed
under GPL-3.0-or-later. Its PS5 package (`PPSA99276.zip`) contains code and
data from the projects below; each keeps its own licence. Licence texts for
vendored code are in the repository at the paths given, and the package
carries `LICENSE` and this file next to `eboot.bin`.

| Component | Version | Licence | Where |
| --- | --- | --- | --- |
| [ProsperoTV](https://github.com/blackbearreloaded/ProsperoTV) native TS demuxer, Videodec2/Audiodec backend, link stubs | commit `3aac8345af6cb4a1188ffbf24be060a184f196ce`, unmodified | GPL-3.0-or-later | `third_party/prosperotv/` (`LICENSE`) |
| [minimp3](https://github.com/lieff/minimp3) (via ProsperoTV) | as vendored by ProsperoTV | CC0-1.0 | `third_party/prosperotv/vendor/minimp3/` |
| [FFmpeg](https://ffmpeg.org/) libavformat, libavcodec, libavutil, libswresample | 8.0.1, built from the verified release source by `tools/setup-ffmpeg.sh` (no `--enable-gpl`/`--enable-nonfree`) | LGPL-2.1-or-later | downloaded at build time; configuration in `tools/setup-ffmpeg.sh` |
| [Inter](https://rsms.me/inter/) typeface (Regular, SemiBold, Bold) | 4.1 | SIL Open Font License 1.1 | `assets/fonts/` (`Inter-LICENSE.txt`) |
| [stb_image / stb_image_write](https://github.com/nothings/stb) | 2.30 / 1.16, commit `2c980bb59875b0d32144a71867fbdebb2f77cd20` | MIT or public domain | `third_party/stb/` (`LICENSE`) |
| [QR Code generator library](https://www.nayuki.io/page/qr-code-generator-library) (C) | as vendored by ProsperoTV | MIT | `third_party/qrcodegen/` |
| [libcurl](https://curl.se/), [OpenSSL](https://www.openssl.org/), [libpsl](https://github.com/rockdaboot/libpsl), [zstd](https://github.com/facebook/zstd), [zlib](https://zlib.net/) | PacBrew v0.40.2 (see the table further down) | curl, Apache-2.0, MIT (+ MPL-2.0 PSL data), BSD-3-Clause, zlib | linked from PacBrew |
| [FreeType](https://freetype.org/), [libpng](http://www.libpng.org/), [bzip2](https://sourceware.org/bzip2/) | PacBrew v0.40.2: FreeType 2.13.2, libpng 1.6.43 | FreeType License (FTL), libpng, bzip2 licence | linked from PacBrew |
| LLVM libc++, libc++abi, libunwind | from the PS5 payload SDK v0.42 | Apache-2.0 WITH LLVM-exception | linked from the SDK |

The bundled test clips in `assets/selftest/` were generated with FFmpeg's
built-in `testsrc`/`testsrc2` and `sine` sources and contain no third-party
material (`h264-aac-360p.mp4` is the `.ts` clip remuxed without re-encoding).
The README screenshots use generated placeholder artwork.

The embedded browser (0.7.0) is the console's own `libSceWebBrowserDialog`,
which AKENO STREAM only calls; no part of it is shipped. Its parameter
layouts and call order were taken, as facts and not as code, from two
GPL-3.0 projects:
[SvenGDK/SharpProspero](https://github.com/SvenGDK/SharpProspero)
(`Interop/Dialog/WebBrowserDialog.cs`, `Platform/WebBrowser.cs`) and
[sainsaji/EVO-PLAYER-PS5](https://github.com/sainsaji/EVO-PLAYER-PS5)
(`src/evo_webui.c`, `docs/research/web-browser-dialog.md`), whose author
verified them on hardware. The YouTube player page uses Google's
[IFrame Player API](https://developers.google.com/youtube/iframe_api_reference)
under the YouTube API Services Terms; the player itself is loaded from
youtube.com at run time.

Catalogue data shown at run time comes from [AniList](https://anilist.co)
(public GraphQL API; data under AniList's terms) and from the
[YouTube Data API](https://developers.google.com/youtube/v3) (under the YouTube
API Services Terms, with the user's own API key). The public test streams in
the Open Streams catalogue are operated by Mux, Bitmovin, Apple and Akamai;
"Big Buck Bunny", "Sintel" and "Tears of Steel" are (c) Blender Foundation,
CC BY 3.0. AKENO STREAM is not affiliated with any of these services, with
Crunchyroll or with Sony.

AKENO STREAM is built on ps5-native-app-boilerplate; its notices follow.

## Credits and acknowledgements

| Project | Role |
| --- | --- |
| [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk) | Public PS5 headers, libc++ headers, sysroot, and Clang target support |
| [ps5-payload-dev/pacbrew-repo](https://github.com/ps5-payload-dev/pacbrew-repo) | Optional prebuilt PS5 ports and static libraries |
| [SvenGDK/SharpProspero](https://github.com/SvenGDK/SharpProspero) | Source of the ELF converter and FSELF writer in `tooling/native/` (GPL-3.0); browser-dialog layouts for 0.7.0 |
| [sainsaji/EVO-PLAYER-PS5](https://github.com/sainsaji/EVO-PLAYER-PS5) | Hardware results for `libSceWebBrowserDialog` in a native title (GPL-3.0) |
| [SvenGDK/UFS2Tool](https://github.com/SvenGDK/UFS2Tool) | Optional UFS2 `.ffpkg` generation |
| [PSBrew/MkPFS](https://github.com/PSBrew/MkPFS) | Optional compressed `.ffpfsc` generation |
| [sinajet/PSFFPKG](https://github.com/sinajet/PSFFPKG) | Public `.ffpkg` procedure used as a format reference |
| [LLVM/Clang](https://github.com/llvm/llvm-project) | Native compiler |
| [GoogleTest](https://github.com/google/googletest) | Pinned host-only C++ unit-test framework |
| [zlib](https://zlib.net/) | Pinned source-built compression library used by the host FSELF tool |
| [Microsoft DirectXTex](https://github.com/microsoft/DirectXTex) | `texconv` presentation-image preparation |
| [FFmpeg](https://ffmpeg.org/) | Developer-supplied selection-audio preparation; AKENO STREAM links its libraries (see above) |
| [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) | Directory-style deployment and hardware validation |
| [ArkSama/PS5-Lapy-JB-Daemon](https://github.com/ArkSama/PS5-Lapy-JB-Daemon) | Original Lapy project and owned-root design |
| [mpereiraesaa/PS5-Lapy-JB-Daemon](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon) | Exact-title one-shot helper and cooperative elevation protocol |

## Native build dependencies

The application build uses LLVM/Clang/lld, zlib 1.3.2, and the public
[PS5 payload SDK](https://github.com/ps5-payload-dev/sdk). The bootstrapper
downloads SDK v0.42 after verifying SHA-256
`8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da`.
It downloads zlib 1.3.2 from the upstream source archive after verifying
SHA-256 `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`
and compiles its static archive locally. Both dependencies remain under ignored
`.deps/native/`, retain their upstream licenses, and are not distributed by
this repository. No Sony SDK file is included.

Target C++ compilation uses the LLVM libc++ headers distributed by the public
SDK. Those headers retain the Apache-2.0 WITH LLVM-exception license recorded
upstream. AKENO STREAM also links the SDK's static libc++, libc++abi and
libunwind archives (same licence) into its executable, as ProsperoTV does.

The PS5 ELF converter and FSELF writer in `tooling/native/` are derived from
[SharpProspero](https://github.com/SvenGDK/SharpProspero), Copyright (C) 2026
SvenGDK, GPL-3.0, and were translated to C++ and modified by BlackBearReloaded.

## Host test dependency

The host unit-test target downloads
[GoogleTest](https://github.com/google/googletest) 1.17.0 after verifying
SHA-256 `65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c`.
It remains under ignored `.deps/test/`, retains its BSD-3-Clause license, and
is not linked into any PS5 application, runtime, or package artifact.

## Optional PS5-Lapy-JB-Daemon integration

The sandbox-elevation build fetches
[mpereiraesaa's PS5-Lapy-JB-Daemon](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon)
at commit `54a095c0f19161825e845daa760a03b446e654fa`, invokes its unmodified
`owned-helper` target for the selected application's exact title and packages
the generated helper ELF with Lapy's MIT license. The shared protocol header
published upstream is LGPL-2.1-or-later; this repository's application-side
wire implementation is GPL-3.0-or-later.

Lapy was created by
[ArkSama / Team PHU](https://github.com/ArkSama/PS5-Lapy-JB-Daemon). Credit
belongs to ArkSama, mpereiraesaa and the Lapy contributors. No Lapy kernel
source is copied or modified here.

The helper build also uses the pinned `ps5log/1` header from
[mpereiraesaa/ps5-agc-gears](https://github.com/mpereiraesaa/ps5-agc-gears/tree/1ae1f9182abd2770c131b97419034fb85173c2dc/native/ps5log),
GPL-3.0-or-later, and the official PS5 Payload SDK v0.40. Those build inputs
remain under ignored `.deps/lapy/`; the normal application toolchain remains
the separately pinned Payload SDK v0.42.

## Optional PacBrew dependencies

When selected through `PACBREW_*` build variables, the build downloads the prebuilt ports image
from [ps5-payload-dev/pacbrew-repo](https://github.com/ps5-payload-dev/pacbrew-repo)
release `v0.40.2`, verifies its published SHA-256, and extracts only the
`target/user/homebrew` prefix under ignored `.deps/pacbrew/`. It does not
replace the pinned SDK or install files globally. PacBrew recipes and every
linked third-party library retain their upstream licenses; applications must
review those terms before redistribution.

The update-check example (`make update-check-example`) selects `libcurl`, which
statically links these PacBrew libraries into the example title. Its build
output stays under ignored `dist/` and is not distributed by this repository; an
application that ships the update check ships them and must carry their notices:

| Component | Version in PacBrew v0.40.2 | License |
| --- | --- | --- |
| [libcurl](https://curl.se/) | 8.18.0 | curl license (MIT/X derivative) |
| [OpenSSL](https://www.openssl.org/) | 3.5.2 | Apache License 2.0 |
| [zlib](https://zlib.net/) | 1.3.2 | zlib license |
| [zstd](https://github.com/facebook/zstd) | 1.5.6 | BSD-3-Clause (dual-licensed with GPL-2.0) |
| [libpsl](https://github.com/rockdaboot/libpsl) | 0.21.5 | MIT; built-in Public Suffix List data MPL-2.0 |

`examples/update-check/console_curl.c`, which makes these libraries run in a
native title, is original BlackBearReloaded code (GPL-3.0-or-later), taken from
the ProsperoRadio and ProsperoLichess projects. Its `gmtime_r` follows Howard
Hinnant's public-domain `civil_from_days` algorithm.

## Vendored miniz

`third_party/miniz/` holds [miniz](https://github.com/richgel999/miniz) 3.0.2
(commit `293d4db1b7d0ffee9756d035b9ac6f7431ef8492`), MIT, unmodified, with its
`LICENSE`; `SOURCE.json` records each file's SHA-256. Only the self-update
helper (`examples/self-update-helper`) and its host test link it; an
application that ships that helper ships miniz and must carry its notice. The
helper's archive validation and file helpers are original BlackBearReloaded
code (GPL-3.0-or-later), taken from the ProsperoStore project.

## Optional UFS2Tool dependency

When `.ffpkg` output is requested, the platform bootstrapper fetches
[SvenGDK/UFS2Tool](https://github.com/SvenGDK/UFS2Tool) at commit
`b5307a60d5b4e3a68ba680e0e33cfadf05017c77` into the ignored
`.deps/UFS2Tool` cache and builds it with the host .NET SDK. UFS2Tool is
BSD-2-Clause software and is not distributed by this repository.

## Optional MkPFS dependency

When `.ffpfsc` output is requested, the platform bootstrapper fetches
[PSBrew/MkPFS](https://github.com/PSBrew/MkPFS) at commit
`6cb8313dfe0c988ac52617794553f343243d3a56` into the ignored `.deps/MkPFS`
cache and installs its Python dependencies into an ignored virtual environment
there. MkPFS and its dependencies retain their own licenses and are not
distributed by this repository.

## Independently authored runtime shim

`tooling/native/libc_builder.cpp` and the manifests under
`tooling/native/runtime/` are independently authored for this project and
licensed under GPL-3.0-or-later. The generated `runtime/libc.prx` contains
project-authored compatibility stubs, startup code, and semantic loader
metadata. It contains no Sony runtime implementation.

Original ps5-native-app-boilerplate code is Copyright (C) 2026
BlackBearReloaded and licensed under GPL-3.0-or-later. Source and script files
carry matching SPDX identifiers.

## Original presentation assets

The BlackBear icon, selection artwork, and default selection track
`sce_sys/snd0.at9` are original assets supplied by BlackBearReloaded, Copyright
(C) 2026 BlackBearReloaded, and distributed under GPL-3.0-or-later. The track
is titled `Night Drive`.

No proprietary runtime module, encryption key, or game file is included.
