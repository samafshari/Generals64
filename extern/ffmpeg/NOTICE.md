# FFmpeg — Third-Party Notice

This directory bundles prebuilt FFmpeg shared libraries (`bin/*.dll`, `lib/*.lib`)
and their public headers. FFmpeg is **not** developed as part of this project.

## License

FFmpeg is distributed here under the **GNU Lesser General Public License,
version 3 or later (LGPL-3.0-or-later)**. The full LGPL-3.0 text is provided
in `LICENSE.txt`. LGPL-3.0 is granted on the additional terms of the GNU
General Public License, version 3 — a full copy of GPL-3.0 is included in
the top-level `LICENSE.md` of this repository.

The libavcodec runtime reports: `libavcodec license: LGPL version 3 or later`.

No GPL-only or non-free components are enabled in this build. In particular,
`--disable-libx264 --disable-libx265 --disable-libxavs2 --disable-libxvid`
were passed and neither `--enable-gpl` nor `--enable-nonfree` was used.

## Version

- libavcodec 61.19.101 (FFmpeg 7.1.x series)
- Build tag / extra-version: `20260329`
- Binaries: `avcodec-61.dll`, `avdevice-61.dll`, `avfilter-10.dll`,
  `avformat-61.dll`, `avutil-59.dll`, `swresample-5.dll`, `swscale-8.dll`

## Source

The corresponding FFmpeg source is available from the upstream project:

- https://ffmpeg.org/download.html (official releases and git)
- https://git.ffmpeg.org/ffmpeg.git (git mirror)

To obtain the exact source matching these binaries, check out the FFmpeg
release tag matching libavcodec 61.19.101 (FFmpeg 7.1.x) and apply the
`./configure` flags listed below.

## Build configuration

These DLLs were produced by an `x86_64-w64-mingw32` cross-compile with the
following `./configure` arguments (as embedded in the binary):

```
--prefix=/ffbuild/prefix --pkg-config-flags=--static --pkg-config=pkg-config
--cross-prefix=x86_64-w64-mingw32- --arch=x86_64 --target-os=mingw32
--enable-version3 --disable-debug --enable-shared --disable-static
--disable-w32threads --enable-pthreads --enable-iconv --enable-zlib
--enable-libxml2 --enable-libvmaf --enable-fontconfig --enable-libharfbuzz
--enable-libfreetype --enable-libfribidi --enable-vulkan --enable-libshaderc
--enable-libvorbis --disable-libxcb --disable-xlib --disable-libpulse
--enable-gmp --enable-lzma --disable-liblcevc-dec --enable-opencl --enable-amf
--enable-libaom --enable-libaribb24 --disable-avisynth --enable-chromaprint
--enable-libdav1d --disable-libdavs2 --disable-libdvdread --disable-libdvdnav
--disable-libfdk-aac --enable-ffnvcodec --enable-cuda-llvm --disable-frei0r
--enable-libgme --enable-libkvazaar --enable-libaribcaption --enable-libass
--enable-libbluray --enable-libjxl --enable-libmp3lame --enable-libopus
--enable-libplacebo --enable-librist --enable-libssh --enable-libtheora
--enable-libvpx --enable-libwebp --enable-libzmq --enable-lv2 --enable-libvpl
--enable-openal --enable-libopencore-amrnb --enable-libopencore-amrwb
--enable-libopenh264 --enable-libopenjpeg --enable-libopenmpt --enable-librav1e
--disable-librubberband --enable-schannel --enable-sdl2 --enable-libsnappy
--enable-libsoxr --enable-libsrt --enable-libsvtav1 --enable-libtwolame
--enable-libuavs3d --disable-libdrm --enable-vaapi --disable-libvidstab
--enable-libvvenc --disable-libx264 --disable-libx265 --disable-libxavs2
--disable-libxvid --enable-libzimg --enable-libzvbi
--extra-cflags=-DLIBTWOLAME_STATIC --extra-libs=-lgomp --extra-ldflags=-pthread
--extra-version=20260329
```

## Replacing the bundled FFmpeg

LGPL-3.0 guarantees you the right to replace the shipped FFmpeg with a modified
version of your own. Because FFmpeg is used via dynamic linking (Windows DLLs),
you can do so by dropping replacement DLLs with the same SONAMEs
(`avcodec-61.dll`, etc.) into the directory next to `generalszh.exe`, provided
they expose an ABI-compatible FFmpeg 7.1 API.

## Patent notice

Some codecs distributed by FFmpeg are covered by software patents in certain
jurisdictions. Redistribution and use of this binary build in those
jurisdictions may require additional licenses. See
https://ffmpeg.org/legal.html for details.
