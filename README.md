# 🎮 Supermodel3-PonMi-Streaming — Spikeout FE 8リンク・WAN ストリーミング

**ランチャーでホストを立てて、ロビーからスロットを選んで、遊ぶだけ。**
**StreamReceiver・XinputReceiver・ViGEmBus は不要になりました。**

> ビルド 20261116 / PonLuncher 2.0.0

---

## 🆕 新しくなったところ

- **Spikeout Final Edition を最大8人でリンクプレイ**（2〜8リンク）。人が足りない台はボットが操作できます
- **クライアントも同じ supermodel.exe** — StreamReceiver は不要。設定画面からロビーを開いてつなぎます
- **新しいランチャー PonLuncher** — リンク数、スロットごとのモード（配信・ローカル・ボット）、コーデックなどを選んで START するだけ
- **Discord 認証** — 決められた Discord サーバーのメンバーだけがホストを立てられ、つなげます
- **部屋のパスワード** — 空白ならパブリック。パスワードのある部屋は、ロビーに鍵のアイコンが出ます
- **Firebase のロビー** — ホストとスロットの空きが自動で一覧に出ます。IP の入力は不要
- **Discord への空き状況の投稿** — ホストのスロットの空きを、Discord のチャンネルに自動で投稿・更新します
- **UPnP で自動ポート開放** — ほとんどの環境でルーターの設定は不要
- **NVENC（NVIDIA）は CUDA Toolkit 不要** — NVIDIA のドライバだけで動きます。NVIDIA 以外の GPU では FFmpeg（x264 / x265、CPU）で配信します
- **H.264 / H.265**、音声は Opus
- **荒らし対策** — 3分操作がないと切断、ホストのキック、1台の PC でクライアントは1つだけ、1つの Discord アカウントは1スロットだけ、古いバージョンはつなげない

---

## ✅ 動作確認済みタイトル

- **Spikeout Final Edition**（spikeofe）— 2〜8リンク、インターネット越しの8人接続を確認

---

## ⚙️ 仕様・制限事項

- **1スロットに1人**（観戦はありません）。使用中のスロットには入れません
- **切断すると、そのキャラはその場で立ち止まります**（ボットには戻りません）。だれかが同じスロットに入ると、その人の操作になります
- **3分間操作がないと自動で切断**されます（最後の30秒は画面に警告が出ます）
- **インターネット越しにつなぐには Discord のログインが必要**です。ログインなしでつなげるのは、同じ PC か家の中のネットワークからだけです
- **クライアントにも ROM が必要**です（ロビーは Spikeout FE を選んだときに開きます）
- コントローラーは、**起動する前につないで** Windows に認識させておいてください（起動後につないだものは認識されません）
- クライアントが使うパッドは、1台目のパッドです
- 8本同時に配信するとホストの負荷が大きくなります。8リンクでは解像度を下げる（960×540 や 640×360）のがおすすめです（参考：RTX 3060 のホストで 8リンク・640×360 を確認）

---

## 📦 同梱内容（ホスト）

```
spikeout8/
├── host/
│   ├── PonLuncher.exe          ← ランチャー（ここから起動）
│   ├── supermodel.exe
│   ├── Config/
│   │   ├── Supermodel_01.ini   ← スロット1（～ Supermodel_08.ini）
│   │   └── …
│   ├── NVRAM/
│   ├── Roms/                   ← ROM ファイルをここに置く
│   └── Assets/
├── costume/                    ← 8人分の服の色
└── waypoints.txt               ← ボットの道順
```

クライアントは、同じ `supermodel.exe` と `Config/Supermodel.ini`、`Roms/` があれば動きます。

> ⚠️ ROM ファイルは含まれていません。各自で合法的に入手してください。

---

## 💻 動作環境

### ホスト

| 項目 | 要件 |
|------|------|
| OS | Windows 10 / 11 64bit |
| CPU | Intel Core i7 以上、または AMD Ryzen 7 以上（シングルスレッドの速さが大切） |
| GPU | NVIDIA（NVENC）推奨。NVIDIA 以外は FFmpeg（CPU）で配信 |
| Discord | 決められた Discord サーバーのメンバーであること |
| ルーター | UPnP 有効（または手動でポート開放） |
| 回線 | 上り 20Mbps 以上推奨（人数が多いほど必要） |

### クライアント

| 項目 | 要件 |
|------|------|
| OS | Windows 10 / 11 64bit |
| Discord | 決められた Discord サーバーのメンバーであること（インターネット越しのとき） |
| その他 | ROM ファイル、XInput か DirectInput のコントローラー（キーボードでも可） |

> テスト環境：Core i7-13700F / RTX 4070 Super 12GB / 64GB RAM / Windows 11（ホストは RTX 3060 の PC でも確認）

---

## 🔌 ポート番号（UDP）

スロット n のポートは `55000 + (n − 1) × 4` から 4 つです。

| スロット | 入力 | セッション | 映像 | 音声 |
|------|-------|-------|-------|-------|
| 1 | 55000 | 55001 | 55002 | 55003 |
| 2 | 55004 | 55005 | 55006 | 55007 |
| 3 | 55008 | 55009 | 55010 | 55011 |
| 4 | 55012 | 55013 | 55014 | 55015 |
| 5 | 55016 | 55017 | 55018 | 55019 |
| 6 | 55020 | 55021 | 55022 | 55023 |
| 7 | 55024 | 55025 | 55026 | 55027 |
| 8 | 55028 | 55029 | 55030 | 55031 |

---

## 🚀 ホストの立て方

### 1. Windows ファイアウォールで許可

`supermodel.exe` の通信を、プライベート・パブリックの両方で許可してください。

### 2. ROM を置く

`host/Roms/` に ROM ファイル（`spikeofe.zip` など）を置きます。

### 3. PonLuncher を起動する

1. **DISCORD**：LOGIN を押して、ブラウザで Discord にログインします。サーバーのメンバーなら「server member」と出ます
2. **HOST**：
   - HOST NAME：ロビーに出るホストの名前
   - GAME：spikeofe
   - LINKS：リンク数（2〜8）
   - CODEC：H264 か H265
   - ENCODER：AUTO（NVIDIA なら NVENC、それ以外は FFmpeg）／ NVENC ／ FFMPEG
   - PASSWORD：部屋のパスワード。**空白ならパブリック**
3. **SLOTS**：スロットごとにモードを選びます

| モード | 内容 |
|--------|------|
| STREAM | インターネット越しの人が遊ぶスロット（配信する） |
| LOCAL | ホストの PC のコントローラーで遊ぶスロット（PAD で XINPUT / DINPUT を選ぶ） |
| BOT | ボットが操作するスロット |

4. **START** で全スロットが起動し、ロビーに載ります。**STOP** で終わります
5. 遊んでいる人は PLAYER に名前が出ます。**KICK** でそのスロットから外せます

> 配信用の画面は最小化してもかまいません。配信の品質には影響しません。

---

## 🕹️ 遊び方（クライアント）

1. `supermodel.exe` を起動して、設定画面で **Spikeout Final Edition** を選び、**LAUNCH**
2. ロビーが開きます。上の **Discord** で LOGIN します
3. ホストの一覧から、**緑のスロット（空き）** を押します
   - 🔒 鍵のアイコンがあるホストは、パスワードの入力欄が出ます
4. つながると、ゲームの画面が出ます

| キー | 内容 |
|------|------|
| F11 | フルスクリーン |
| F3 | 遅延などの表示 |
| ↑ / ↓ | ビットレートを上げる・下げる |
| ESC | フルスクリーン → ウィンドウ → ロビーに戻る |

> 同じ PC や家の中のホストには、下の「Connect directly」で IP を入れてつなぐこともできます。

---

## 🔧 設定

### 映像のコーデック

ランチャーの CODEC で選びます（ini では `Codec = H264` / `Codec = H265`）。

| コーデック | 設定値 | 備考 |
|-----------|--------|------|
| H.264 (AVC) | `H264` | 既定。負荷が軽く、どの PC でも動きます |
| H.265 (HEVC) | `H265` | 同じ画質でビットレートを抑えられます。クライアントの CPU の負荷は少し上がります |

> クライアントは CPU で映像を読み解くので、クライアントの GPU は問いません。

### エンコーダ

| 設定値 | 内容 |
|--------|------|
| `AUTO` | NVIDIA の GPU があれば NVENC、なければ FFmpeg |
| `NVENC` | NVIDIA の GPU で配信（低遅延。CUDA Toolkit は不要、ドライバだけで動作） |
| `FFMPEG` | CPU で配信（x264 / x265）。どの GPU でも動きます |

---

## ⚠️ Windows SmartScreen について

コード署名証明書が未取得のため、初回起動時に SmartScreen の警告が表示される場合があります。
「詳細情報」→「実行」で起動できます。

---

## 📜 ライセンス

MIT License

Copyright (c) 2026 BackPonBeauty

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

Based on the original [Supermodel3](https://github.com/trzy/Supermodel)
License: GPL Ver.3
Copyright 2003-2026 The Supermodel Team

## 使用ライブラリ

このソフトウェアは、次のライブラリを使っています（~~打ち消し線~~ は、今のバージョンでは使っていないもの）。

### [Firebase](https://firebase.google.com/)
- Realtime Database と Authentication を REST で使用（SDK は使っていません）
- License: https://firebase.google.com/terms
- Copyright 2023 Google LLC.

### ~~[Newtonsoft.Json](https://www.newtonsoft.com/json)~~
- ~~License: MIT~~
- ~~Copyright (c) 2007 James Newton-King~~

### ~~[SharpDX](http://sharpdx.org/)~~
- ~~License: MIT~~
- ~~Copyright (c) 2010-2015 SharpDX - Alexandre Mutel~~

### ~~[System.Reactive](https://github.com/dotnet/reactive)~~
- ~~License: MIT~~
- ~~Copyright (c) .NET Foundation and Contributors~~

### [miniupnpc](https://github.com/miniupnp/miniupnp)
- [License: BSD 3-Clause License](https://github.com/miniupnp/miniupnp/blob/master/LICENSE)
- Copyright (c) 2005-2022, Thomas BERNARD. All rights reserved.

### ~~[ViGEm Bus Driver](https://vigem.org/)~~
- ~~[License: MIT License](https://github.com/ViGEm/ViGEmBus/blob/master/LICENSE)~~
- ~~Copyright (c) 2016-2022 Nefarius Software Solutions e.U.~~

### [NVIDIA CUDA Toolkit](https://developer.nvidia.com/cuda-toolkit)
- ビルドのときに CUDA ドライバ API のヘッダー（`cuda.h`）だけを使用。実行には不要（CUDA ランタイムは使っていません）
- [License: CUDA Toolkit EULA](https://docs.nvidia.com/cuda/eula/index.html)
- Copyright (c) 2020 NVIDIA Corporation. All rights reserved.

### [NVIDIA Video Codec SDK](https://developer.nvidia.com/nvidia-video-codec-sdk)
- [License: NVENC SDK License Agreement](https://developer.nvidia.com/nvidia-video-codec-sdk-license-agreement)
- Copyright (c) 2020 NVIDIA Corporation. All rights reserved.

### ~~[Concentus & Concentus.Oggfile](https://github.com/lostindetails/Concentus)~~
- ~~License: Microsoft Public License (MS-PL)~~
- ~~Copyright (c) 2015 Logan Stromberg~~

### ~~[LiteDB](https://www.litedb.org/)~~
- ~~[License: MIT License](https://github.com/mbdavid/LiteDB/blob/master/LICENSE)~~
- ~~Copyright (c) 2015 Maurício David~~

### ~~[NAudio](https://github.com/naudio/NAudio)~~
- ~~[License: MIT License](https://github.com/naudio/NAudio/blob/master/LICENSE)~~
- ~~Copyright (c) 2020 Mark Heath~~

### ~~[Mono.Nat](https://github.com/mono/mono.nat)~~
- ~~[License: MIT License](https://github.com/mono/mono.nat/blob/master/LICENSE)~~
- ~~Copyright (c) Alan McGovern and Contributors~~

### [FFmpeg](https://www.ffmpeg.org/)
- このビルドは x264 / x265 を含めて作っているため GPL（`--enable-gpl`）
- 版：`master` のコミット `a5e6c01`（2026-07-15、`8.0.git`）。作り方は [Docs/BUILD_FFMPEG.md](Docs/BUILD_FFMPEG.md)
- [License: GPL ver.2 or later（このビルド）／ LGPL ver.2.1 or later（FFmpeg 本体）](https://www.ffmpeg.org/legal.html)
- Copyright (c) 2000-2026 the FFmpeg developers

### [x264](https://www.videolan.org/developers/x264.html)（追加）
- H.264 のソフトウェアエンコード（FFMPEG エンコーダ）
- 版：0.165.r3222.b35605a（MSYS2 のパッケージ、静的ライブラリ）
- [License: GPL ver.2 or later](https://code.videolan.org/videolan/x264/-/blob/master/COPYING)
- Copyright (c) the x264 project

### [x265](https://www.videolan.org/developers/x265.html)（追加）
- H.265 のソフトウェアエンコード（FFMPEG エンコーダ）
- 版：4.1（MSYS2 のパッケージ、静的ライブラリ）
- [License: GPL ver.2 or later](https://bitbucket.org/multicoreware/x265_git/src/master/COPYING)
- Copyright (c) MulticoreWare, Inc

### ~~[FirebaseDatabase.net](https://github.com/step-up-labs/firebase-database-dotnet)~~
- ~~[License: MIT License](https://github.com/step-up-labs/firebase-database-dotnet/blob/master/LICENSE)~~
- ~~Copyright (c) 2017 Step Up Labs, Inc.~~

### ~~[FirebaseAuthentication.net](https://github.com/step-up-labs/firebase-authentication-dotnet)~~
- ~~[License: MIT License](https://github.com/step-up-labs/firebase-authentication-dotnet/blob/master/LICENSE)~~
- ~~Copyright (c) 2017 Step Up Labs, Inc.~~

### [SDL2](https://www.libsdl.org/)
- [License: zlib License](https://www.libsdl.org/license.php)
- Copyright (c) 1997-2026 Sam Lantinga

### [SDL2_net](https://github.com/libsdl-org/SDL2_net)
- [License: zlib License](https://github.com/libsdl-org/SDL2_net/blob/main/LICENSE.txt)
- Copyright (c) 1997-2026 Sam Lantinga

### [zlib](https://zlib.net/)
- [License: zlib License](https://zlib.net/zlib_license.html)
- Copyright (c) 1995-2026 Jean-loup Gailly and Mark Adler

### [Minizip](https://github.com/madler/zlib/tree/master/contrib/minizip)（追加）
- ROM の zip を読む
- License: zlib License
- Copyright (c) 1998-2010 Gilles Vollant

### [Dear ImGui](https://github.com/ocornut/imgui)
- [License: MIT License](https://github.com/ocornut/imgui/blob/master/LICENSE.txt)
- Copyright (c) 2014-2026 Omar Cornut

### [TinyXML-2](https://github.com/leethomason/tinyxml2)
- [License: zlib License](https://github.com/leethomason/tinyxml2/blob/master/LICENSE.txt)
- Copyright (c) 2011-2026 Lee Thomason

### [GLEW](https://glew.sourceforge.net/)（追加）
- [License: Modified BSD License / MIT License](https://github.com/nigels-com/glew/blob/master/LICENSE.txt)
- Copyright (c) the GLEW authors (Milan Ikits, Marcelo Magallon, Nigel Stewart ほか)

### [minimp3](https://github.com/lieff/minimp3)（追加）
- [License: CC0 1.0](https://github.com/lieff/minimp3/blob/master/LICENSE)
- lieff

### [libiconv](https://www.gnu.org/software/libiconv/)（追加）
- [License: LGPL ver.2.1 or later](https://www.gnu.org/software/libiconv/)
- Copyright (c) Free Software Foundation, Inc.

### [Opus](https://www.opus-codec.org/)
- [License: BSD 3-Clause License](https://github.com/xiph/opus/blob/master/COPYING)
- Copyright (c) 2001-2026 Xiph.Org Foundation, Skype Limited, Octasic, Jean-Marc Valin, Timothy B. Terriberry, CSIRO, Gregory Maxwell, Mark Borgerding, Erik de Castro Lopo

### [Musashi M68K Emulator](https://github.com/kstenerud/Musashi)
- [License: MIT License](https://github.com/kstenerud/Musashi/blob/master/LICENSE)
- Copyright (c) 2011 Karl Stenerud

### 使用しているサービス（追加）
- [Discord](https://discord.com/)：ログイン（OAuth2 / PKCE）、サーバーのメンバーの確認、Webhook での空き状況の投稿 — [Discord Developer Terms of Service](https://discord.com/developers/docs/policies-and-agreements/developer-terms-of-service)

---

## 💬 コミュニティ

- [BackPonBeauty](https://github.com/BackPonBeauty) — GitHub
- [patreon.com/PonMi](https://patreon.com/PonMi) — Patreon
- [discord.gg/mNjPJHTTen](https://discord.gg/mNjPJHTTen) — Discord
- [back_pon_beauty](https://twitch.tv/back_pon_beauty) — Twitch
