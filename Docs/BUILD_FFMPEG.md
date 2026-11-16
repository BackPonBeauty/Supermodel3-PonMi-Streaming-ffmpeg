# Building FFmpeg / x264 / x265 (versions and steps)

supermodel.exe links FFmpeg (libavcodec, libavutil, libswscale), x264 and x265 **statically** (no DLLs).
FFmpeg is configured with `--enable-gpl`, so this FFmpeg build and the supermodel.exe that contains it are distributed under the **GPL**.
This page gives the versions and the steps to rebuild the same thing (it also serves as the GPL "Corresponding Source" guide).

---

## Versions

| Component | Version | Source |
|-----------|---------|--------|
| FFmpeg | `master` commit `a5e6c0175a7245fc1a9f4639da2810156128aa8b` (2026-07-15, reports itself as `8.0.git`). No local changes | https://github.com/FFmpeg/FFmpeg |
| &nbsp;&nbsp;libavcodec | 63.6.101 | (part of FFmpeg) |
| &nbsp;&nbsp;libavutil | 61.5.100 | (part of FFmpeg) |
| &nbsp;&nbsp;libswscale | 10.2.100 | (part of FFmpeg) |
| x264 | 0.165.r3222.b35605a (MSYS2 package `mingw-w64-x86_64-x264` 0.165.r3222.b35605a-2) | https://code.videolan.org/videolan/x264 (commit `b35605a`) |
| x265 | 4.1 (MSYS2 package `mingw-w64-x86_64-x265` 4.1-2) | https://bitbucket.org/multicoreware/x265_git |

x264 and x265 are the static libraries of the MSYS2 packages (`/mingw64/lib/libx264.a`, `libx265.a`), used as they are. The package build scripts (PKGBUILD) are at https://github.com/msys2/MINGW-packages.

### Build tools

| Tool | Version |
|------|---------|
| MSYS2 (MINGW64 environment) | — |
| gcc | 15.2.0 (`mingw-w64-x86_64-gcc` 15.2.0-8) |
| NASM | 2.16.03 |

### Other libraries linked statically (MSYS2 packages)

| Library | Version | License |
|---------|---------|---------|
| SDL2 | 2.32.10 | zlib |
| SDL2_net | 2.2.0 | zlib |
| Opus | 1.5.2 | BSD 3-Clause |
| miniupnpc | 2.3.3 | BSD 3-Clause |
| zlib | 1.3.1 | zlib |
| libiconv | 1.18 | LGPL 2.1 or later |

> libiconv (LGPL) is linked statically, so the full source of supermodel.exe and these steps are published, allowing users to relink it.

---

## Steps

Run everything in the MSYS2 **MINGW64** shell.

### 1. Install the tools and libraries

```bash
pacman -S --needed make git \
  mingw-w64-x86_64-gcc mingw-w64-x86_64-nasm mingw-w64-x86_64-pkgconf \
  mingw-w64-x86_64-x264 mingw-w64-x86_64-x265 \
  mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_net mingw-w64-x86_64-opus \
  mingw-w64-x86_64-miniupnpc mingw-w64-x86_64-zlib mingw-w64-x86_64-libiconv
```

> To match the versions in the tables above exactly, install the older MSYS2 packages from https://repo.msys2.org/mingw/mingw64/ with `pacman -U`.

### 2. Get the FFmpeg source

```bash
cd ~
git clone https://github.com/FFmpeg/FFmpeg ffmpeg-src
cd ffmpeg-src
git checkout a5e6c0175a7245fc1a9f4639da2810156128aa8b
```

### 3. configure

A static-only configuration with just what is used (encoders: x264 and x265; decoders: H.264 and HEVC).

```bash
./configure --prefix=/opt/ffmpeg-static-dec \
  --enable-static --disable-shared \
  --disable-everything --disable-autodetect \
  --enable-gpl \
  --enable-encoder=libx264 --enable-encoder=libx265 \
  --enable-decoder=h264 --enable-decoder=hevc \
  --enable-parser=h264 --enable-parser=hevc \
  --enable-swscale --enable-protocol=file \
  --enable-libx264 --enable-libx265 \
  --disable-programs --disable-doc \
  --pkg-config-flags=--static \
  --extra-cflags=-I/mingw64/include \
  --extra-ldflags='-L/mingw64/lib -static'
```

### 4. Build and install

```bash
make -j8
make install
```

This creates `include/` and `lib/libavcodec.a` etc. in `/opt/ffmpeg-static-dec` (on Windows: `C:\msys64\opt\ffmpeg-static-dec`).

### 5. Build supermodel.exe

`FFMPEG_STATIC_DIR` in `Makefiles/Makefile.Win32` points to that location (`C:/msys64/opt/ffmpeg-static-dec`).
Put `C:\msys64\mingw64\bin;C:\msys64\usr\bin` at the front of PATH and run the following at the top of the repository; it produces `bin64\supermodel.exe`.

```bash
mingw32-make -f Makefiles/Makefile.Win32 -j8
```

> For NVENC, the CUDA header (`cuda.h`, from the CUDA Toolkit) is used at build time only (`CUDA_INC_DIR` in `Makefiles/Makefile.Win32`). The CUDA Toolkit is not needed to run the program.
> The Firebase and Discord keys are embedded into the exe at build time from `Config/StreamingKeys.ini` (not in git; see `Config/StreamingKeys.example.ini` for its format). The build also works without this file; the program then runs without Firebase and Discord.
> The recoloured costume pictures (`Config/costume/`) are not in git either. Without them the build still works (an empty set is embedded).

---

## Checking the build

The configuration of the FFmpeg you built is in `FFMPEG_CONFIGURATION` in `config.h`, or can be checked with:

```bash
grep FFMPEG_CONFIGURATION ~/ffmpeg-src/config.h
grep ^Version /opt/ffmpeg-static-dec/lib/pkgconfig/libavcodec.pc
```
