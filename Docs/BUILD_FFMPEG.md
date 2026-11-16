# FFmpeg / x264 / x265 の作り方と版

supermodel.exe は、FFmpeg（libavcodec・libavutil・libswscale）と x264・x265 を**静的に組み込んで**います（DLL は使いません）。
FFmpeg は `--enable-gpl` で作っているため、このビルドの FFmpeg と、それを組み込んだ supermodel.exe は **GPL** の条件で配布します。
ここに、同じものを作り直すための版と手順を書きます（GPL の「対応するソース」の案内を兼ねます）。

---

## 版

| もの | 版 | 入手先 |
|------|-----|--------|
| FFmpeg | `master` のコミット `a5e6c0175a7245fc1a9f4639da2810156128aa8b`（2026-07-15、版の表示は `8.0.git`）。手元での変更なし | https://github.com/FFmpeg/FFmpeg |
| 　libavcodec | 63.6.101 | （FFmpeg に含まれる） |
| 　libavutil | 61.5.100 | （FFmpeg に含まれる） |
| 　libswscale | 10.2.100 | （FFmpeg に含まれる） |
| x264 | 0.165.r3222.b35605a（MSYS2 の `mingw-w64-x86_64-x264` 0.165.r3222.b35605a-2） | https://code.videolan.org/videolan/x264 （コミット `b35605a`） |
| x265 | 4.1（MSYS2 の `mingw-w64-x86_64-x265` 4.1-2） | https://bitbucket.org/multicoreware/x265_git |

x264 と x265 は MSYS2 のパッケージの静的ライブラリ（`/mingw64/lib/libx264.a`・`libx265.a`）をそのまま使っています。パッケージの作り方（PKGBUILD）は https://github.com/msys2/MINGW-packages にあります。

### 作るのに使った道具

| もの | 版 |
|------|-----|
| MSYS2（MINGW64 の環境） | — |
| gcc | 15.2.0（`mingw-w64-x86_64-gcc` 15.2.0-8） |
| NASM | 2.16.03 |

### いっしょに静的に組み込んでいるもの（MSYS2 のパッケージ）

| もの | 版 | ライセンス |
|------|-----|-----------|
| SDL2 | 2.32.10 | zlib |
| SDL2_net | 2.2.0 | zlib |
| Opus | 1.5.2 | BSD 3-Clause |
| miniupnpc | 2.3.3 | BSD 3-Clause |
| zlib | 1.3.1 | zlib |
| libiconv | 1.18 | LGPL 2.1 以降 |

> libiconv（LGPL）を静的に組み込んでいるので、利用者が組み直せるよう、supermodel.exe のソース全体とこの手順を公開しています。

---

## 手順

MSYS2 の **MINGW64** のシェルで行います。

### 1. 道具とライブラリを入れる

```bash
pacman -S --needed make git \
  mingw-w64-x86_64-gcc mingw-w64-x86_64-nasm mingw-w64-x86_64-pkgconf \
  mingw-w64-x86_64-x264 mingw-w64-x86_64-x265 \
  mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_net mingw-w64-x86_64-opus \
  mingw-w64-x86_64-miniupnpc mingw-w64-x86_64-zlib mingw-w64-x86_64-libiconv
```

> 上の表と同じ版にそろえるときは、MSYS2 のパッケージの古い版（https://repo.msys2.org/mingw/mingw64/ ）を `pacman -U` で入れてください。

### 2. FFmpeg のソースを取ってくる

```bash
cd ~
git clone https://github.com/FFmpeg/FFmpeg ffmpeg-src
cd ffmpeg-src
git checkout a5e6c0175a7245fc1a9f4639da2810156128aa8b
```

### 3. configure

使うものだけを入れた、静的ライブラリだけの設定です（エンコーダは x264・x265、デコーダは H.264・HEVC）。

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

### 4. 作って入れる

```bash
make -j8
make install
```

`/opt/ffmpeg-static-dec`（Windows では `C:\msys64\opt\ffmpeg-static-dec`）に、`include/` と `lib/libavcodec.a` などができます。

### 5. supermodel.exe を作る

`Makefiles/Makefile.Win32` の `FFMPEG_STATIC_DIR` が、上の場所を指しています（`C:/msys64/opt/ffmpeg-static-dec`）。
PATH の先頭に `C:\msys64\mingw64\bin;C:\msys64\usr\bin` を足して、リポジトリの一番上で次を実行すると、`bin64\supermodel.exe` ができます。

```bash
mingw32-make -f Makefiles/Makefile.Win32 -j8
```

> NVENC を使うために、CUDA のヘッダー（`cuda.h`、CUDA Toolkit に入っているもの）をビルドのときだけ使います（`Makefiles/Makefile.Win32` の `CUDA_INC_DIR`）。実行には CUDA Toolkit は要りません。
> Firebase・Discord のキーは `Config/StreamingKeys.ini`（git に入れないファイル。形は `Config/StreamingKeys.example.ini`）から、ビルドのときに exe に埋め込まれます。このファイルがなくてもビルドはでき、その場合は Firebase と Discord を使わずに動きます。

---

## 確かめ方

作った FFmpeg の設定は、`config.h` の `FFMPEG_CONFIGURATION` か、次で見られます。

```bash
grep FFMPEG_CONFIGURATION ~/ffmpeg-src/config.h
grep ^Version /opt/ffmpeg-static-dec/lib/pkgconfig/libavcodec.pc
```
