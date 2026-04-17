# Third-Party Licenses

This project redistributes, in the `extern/` directory, prebuilt binaries
and/or source for the following third-party components. Each component is
governed by its own license, listed below. Where the component ships its own
`LICENSE`, `COPYING`, or equivalent file, that file is authoritative.

The main game source (everything outside `extern/`) is GPL-3.0 with additional
terms — see the top-level `LICENSE.md`.

| Component                 | License                       | Notes                                                                                     |
|---------------------------|-------------------------------|-------------------------------------------------------------------------------------------|
| `extern/ffmpeg/`          | LGPL-3.0-or-later             | Prebuilt DLLs + headers. See `extern/ffmpeg/NOTICE.md` and `extern/ffmpeg/LICENSE.txt`.   |
| `extern/sdl3/`            | Zlib                          | Prebuilt binaries + headers. See the SDL3 license header in its source distribution.      |
| `extern/vulkan/`          | Apache-2.0 / MIT (registry)   | Vulkan headers vendored from the Khronos registry.                                        |
| `extern/imgui/`           | MIT                           | Dear ImGui. See `extern/imgui/LICENSE.txt` in the upstream distribution.                  |
| `extern/json/`            | MIT                           | nlohmann/json single-header.                                                              |
| `extern/sqlite3/`         | Public domain                 | SQLite amalgamation.                                                                      |
| `extern/zlib/`            | zlib license                  | zlib reference implementation.                                                            |
| `extern/d3d11/`           | Project-owned                 | Custom replacement headers for legacy D3D8/DirectInput types; not copied from MS SDKs.    |
| `extern/lzhl_wrapper/`    | LZH-Light 1.0 terms           | LZH-Light wrapper carried over from the original Command & Conquer Generals source drop. |

## FFmpeg / LGPL-3.0 compliance

The FFmpeg build shipped in `extern/ffmpeg/bin/` is an LGPL-3.0-or-later build
with **no** GPL-only components enabled. See `extern/ffmpeg/NOTICE.md` for:

- the exact FFmpeg version and build tag,
- the full `./configure` argument list,
- where to obtain the corresponding upstream source,
- how to drop in a replacement FFmpeg DLL set (as LGPL-3.0 entitles you to do).

The LGPL-3.0 text is in `extern/ffmpeg/LICENSE.txt`. The GPL-3.0 text it
references is included in the top-level `LICENSE.md`.

## Trademarks

"Command & Conquer" and "Generals" are trademarks of Electronic Arts Inc. No
trademark license is granted by this repository's license. This project is not
endorsed by or affiliated with Electronic Arts Inc.
