# 🎮 Supermodel3-PonMi-Streaming v2.0.0 - Built-in WAN Streaming

**No extra tools required. Just launch, share your slot, and play.**

---

## 🆕 What's New in v2.0.0

- **Streaming built directly into the emulator** — XinputReciever is no longer required
- **Firebase automatic matchmaking** — hosts are discovered automatically, no manual IP entry needed
- **UPnP automatic port forwarding** — no router configuration required in most cases
- **Ping display** — latency to each host is shown in the host list
- **NVENC H.264 / H.265 low-latency streaming** — ~5.8ms encode latency on RTX 20 series or later
- **Up to 4-player WAN link play** (mix of local and remote players supported)

---

## ✅ Confirmed Working Titles

- **Spikeout Final Edition** (spikeofe) — 4-link play confirmed
- Virtua Fighter 3tb (single instance)

---

## ⚙️ Specifications & Limitations

- **XInput only** — titles requiring mouse or light gun input are not supported
- **Up to 3 connections per instance** — one player + up to 2 spectators, or 3 spectators
- If the player disconnects, the next spectator in line becomes the player
- **Auto-disconnect** — clients with no XInput input for 1 minute will be disconnected

---

## 📦 Package Contents

This package is configured for **Spikeout Final Edition (spikeofe) 4-link play**.

```
Spikeofe_4links_Sample_20260613.zip
├── 01/
│   ├── supermodel.exe
│   └── Config/
│       └── Supermodel.ini   ← Slot P1
├── 02/                       ← Slot P2
├── 03/                       ← Slot P3
├── 04/                       ← Slot P4
├── ROMs/                     ← Place your ROM files here
├── 4lnkstart.bat             ← Launch all 4 instances
├── 4lnkstart with cmd.bat    ← Launch all 4 instances (with console window)
└── README.md
```

### Streaming Configuration Patterns

**All remote** (host does not play)

| Slot | Streaming |
|------|-----------|
| 01–04 | `Streaming = 1` |

> StreamReceiver is not required on the host side.

**P1 local + P2–P4 remote** (host plays as P1)

| Slot | Streaming |
|------|-----------|
| 01 | `Streaming = 0` |
| 02–04 | `Streaming = 1` |

> ⚠️ Connect your XInput controller and make sure Windows recognizes it before launching.

> ⚠️ ROM files are not included. Please obtain them legally on your own.

---

## 💻 Requirements

### Host

| Item | Requirement |
|------|-------------|
| OS | Windows 10/11 64-bit |
| GPU | NVIDIA RTX 20 series or later (NVENC required) |
| Driver | CUDA 13.0 compatible or later |
| Other | ViGEmBus |
| Router | UPnP enabled (or manual port forwarding) |
| Network | 20 Mbps upload or faster recommended |

- [ViGEmBus](https://github.com/nefarius/ViGEm.Bus/releases) — Virtual gamepad driver

> Tested on: Core i7-13700F / RTX 4070 Super 12GB / 64GB RAM / 10GbE / Windows 11  
> Minimum recommended: Core i5 9th gen or later

---

## 🔌 Port Numbers

| Slot | XInput | HS/HB | Video | Audio |
|------|--------|-------|-------|-------|
| P1 | 5000 | 5001 | 5002 | 5003 |
| P2 | 5004 | 5005 | 5006 | 5007 |
| P3 | 5008 | 5009 | 5010 | 5011 |
| P4 | 5012 | 5013 | 5014 | 5015 |

---

## 🚀 Setup (Host)

### 1. Install ViGEmBus

Download and install the latest release from [ViGEmBus Releases](https://github.com/nefarius/ViGEm.Bus/releases).

### 2. Allow through Windows Firewall

Allow `supermodel.exe` through Windows Firewall for both private and public networks.

### 3. Configure Supermodel.ini

Set the following in each slot's `Config/Supermodel.ini`:

```ini
Streaming = 1
LinkPlay = 1   ; your slot number (1–4)
```

#### LinkPlay values

| Value | Description | Virtual controllers created |
|-------|-------------|----------------------------|
| `0` | Single title — streams both P1 and P2 from one instance | 2 (P1 + P2) |
| `1` | Link play slot 1 (P1) / Single title: P1 local + P2 streaming | 1 |
| `2` | Link play slot 2 (P2) | 1 |
| `3` | Link play slot 3 (P3) | 1 |
| `4` | Link play slot 4 (P4) | 1 |

> For single-title streaming (e.g. a 2-player cabinet), use `LinkPlay = 0`. Two virtual controllers will be created on the client side.

> For single-title with P1 played locally on the host, use `LinkPlay = 1` and `Streaming = 1`. Connect your XInput controller before launching.

### 4. Place your ROM

Place your ROM files in the **`ROMs/`** folder at the root of the package. The sample `Supermodel.ini` in each slot is already configured to point to this directory.

### 5. Launch supermodel.exe

Run **`4lnkstart.bat`** to launch all 4 instances at once. This is the recommended way to start.

> `4lnkstart with cmd.bat` opens a console window for debugging purposes.

---

> Streaming instances can be minimized — audio can also be muted in Windows mixer. This will not affect streaming performance.

Clients use [StreamReceiver](https://github.com/BackPonBeauty/StreamReceiver) to join.

---

## 🔧 Configuration

### Video Codec (`Supermodel.ini`)

You can select the video codec used for streaming by editing `Config/Supermodel.ini`:

```ini
Decoder = H265   ; Use H.265 (HEVC) — better quality at lower bitrate
Decoder = H264   ; Use H.264 (AVC)  — wider compatibility (default)
```

| Codec | Key | Notes |
|-------|-----|-------|
| H.264 (AVC) | `H264` | Default. Broadest compatibility. |
| H.265 (HEVC) | `H265` | Lower bitrate at equivalent quality. Requires NVENC-capable GPU on host. |

> **Note:** The key name is `Decoder` in the config file, but it should have been `Codec`. This is a known typo.

> Both host and client must support the selected codec.  
> H.265 requires an NVIDIA RTX 20 series or later GPU on the host side.

### H.264 vs H.265 Requirements

| Role | H.264 (default) | H.265 (HEVC) |
|------|-----------------|--------------|
| **Host GPU** | GTX 600 series or later (Kepler, 2012+) | GTX 960 / 950 or later, GTX 10 series or later (Maxwell 2nd gen, 2015/2016+) |
| **Client GPU/CPU** | Compatible with virtually any PC (even ~2011 hardware) | Intel 6th gen CPU or later, or GTX 960 / GTX 10 series or later |

H.264's strength is that it works reliably on older, lower-spec hardware. With H.265, any standard PC released within the last ~10 years can enjoy higher quality streaming at lower bandwidth.

> ⚠️ *Requirements above are based on AI-generated information and may not be fully accurate. Please verify against official NVIDIA/Intel documentation if needed.*

---

## 🖥️ Client App

- [StreamReceiver](https://github.com/BackPonBeauty/StreamReceiver) — Client app (video reception & controller input)

---

## 🔧 Related Repositories

- [StreamReceiver](https://github.com/BackPonBeauty/StreamReceiver) — Client app (video reception & controller input)
- [Supermodel3-PonMi](https://github.com/BackPonBeauty/Supermodel3-PonMi) — Base PonMi edition emulator

---

## ⚠️ Windows SmartScreen

A SmartScreen warning may appear on first launch due to the absence of a code signing certificate.  
Click **"More info" → "Run anyway"** to proceed.

---

## 📜 License

This project is released under the **GPL v3** license.  
Based on the original [Supermodel3](https://www.supermodel3.com).

### Third-party Libraries

- ffmpeg `ffmpeg-2026-06-01-git-bf608f16fd-essentials_build` — [LGPL 2.1 / GPL 2.0](https://ffmpeg.org) / Build by [gyan.dev](https://www.gyan.dev/ffmpeg/builds/)
- NVENC / CUDA — NVIDIA License
- [NVIDIA Video Codec SDK](https://developer.nvidia.com/nvidia-video-codec-sdk-license-agreement) — NVIDIA Toolkit License
- miniupnpc — BSD License
- ViGEm — BSD License
- Firebase C++ SDK — Apache 2.0

---

## 💬 Community

- [BackPonBeauty](https://github.com/BackPonBeauty) — GitHub
- [patreon.com/PonMi](https://patreon.com/PonMi) — Patreon
- [discord.gg/mNjPJHTTen](https://discord.gg/mNjPJHTTen) — Discord
- [back_pon_beauty](https://twitch.tv/back_pon_beauty) — Twitch

---
---

# 🎮 Supermodel3-PonMi-Streaming v2.0.0 - Built-in WAN Streaming

**追加ツール不要。起動して、スロットを共有して、プレイするだけ。**

---

## 🆕 v2.0.0 の新機能

- **ストリーミング機能をエミュレータに直接内蔵** — XinputReciever は不要になりました
- **Firebase 自動マッチメイキング** — ホストを自動検出、手動でのIP入力は不要
- **UPnP 自動ポート開放** — ほとんどの環境でルーター設定が不要
- **Ping 表示** — ホスト一覧に各ホストへの遅延を表示
- **NVENC H.264 / H.265 低遅延ストリーミング** — RTX 20 系以降で約 5.8ms のエンコード遅延
- **最大4人 WAN リンクプレイ対応**（ローカルとリモートの混在可）

---

## ✅ 動作確認済みタイトル

- **Spikeout Final Edition**（spikeofe）— 4リンクプレイ確認済み
- Virtua Fighter 3tb（シングルインスタンス）

---

## ⚙️ 仕様・制限事項

- **XInput対応タイトルのみ** — マウス操作・ライトガン使用タイトルは非対応
- **各インスタンスへの接続は最大3人まで** — プレーヤー1人＋観戦者最大2人、または観戦者3人
- プレーヤーが切断した場合、次の観戦者が自動的にプレーヤーになります
- **自動切断** — 1分間XInputの入力がない場合、自動的に切断されます

---

## 📦 同梱内容

本パッケージは **Spikeout Final Edition（spikeofe）4リンクプレイ用**に構成されています。

```
Spikeofe_4links_Sample_20260613.zip
├── 01/
│   ├── supermodel.exe
│   └── Config/
│       └── Supermodel.ini   ← P1スロット
├── 02/                       ← P2スロット
├── 03/                       ← P3スロット
├── 04/                       ← P4スロット
├── ROMs/                     ← ROMファイルをここに配置
├── 4lnkstart.bat             ← 4インスタンス一括起動
├── 4lnkstart with cmd.bat    ← 4インスタンス一括起動（コンソールあり）
└── README.md
```

### ストリーミング設定パターン

**全員リモートの場合**（ホストは操作しない）

| スロット | Streaming |
|---------|-----------|
| 01〜04 | `Streaming = 1` |

> StreamReceiver はホスト側では不要です。

**P1ローカル＋P2〜P4リモートの場合**（ホストがP1としてプレイ）

| スロット | Streaming |
|---------|-----------|
| 01 | `Streaming = 0` |
| 02〜04 | `Streaming = 1` |

> ⚠️ 起動前に XInput コントローラーを接続し、Windows に認識させておいてください。

> ⚠️ ROMファイルは含まれていません。各自で合法的に入手してください。

---

## 💻 動作環境

### ホスト側

| 項目 | 要件 |
|------|------|
| OS | Windows 10/11 64bit |
| GPU | NVIDIA RTX 20系以降（NVENC必須） |
| ドライバ | CUDA 13.0 対応以降 |
| その他 | ViGEmBus |
| ルーター | UPnP有効（または手動ポート開放） |
| 回線 | アップロード 20Mbps 以上推奨 |

- [ViGEmBus](https://github.com/nefarius/ViGEm.Bus/releases) — 仮想ゲームパッドドライバ

> テスト環境：Core i7-13700F / RTX 4070 Super 12GB / 64GB RAM / 10GbE / Windows 11  
> 最低推奨：Core i5 第9世代以上

---

## 🔌 ポート番号

| スロット | XInput | HS/HB | Video | Audio |
|---------|--------|-------|-------|-------|
| P1 | 5000 | 5001 | 5002 | 5003 |
| P2 | 5004 | 5005 | 5006 | 5007 |
| P3 | 5008 | 5009 | 5010 | 5011 |
| P4 | 5012 | 5013 | 5014 | 5015 |

---

## 🚀 セットアップ（ホスト）

### 1. ViGEmBus をインストール

[ViGEmBus Releases](https://github.com/nefarius/ViGEm.Bus/releases) から最新版をダウンロードしてインストール。

### 2. Windowsファイアウォールで許可

`supermodel.exe` をプライベート・パブリック両方のネットワークで通信を許可してください。

### 3. Supermodel.ini を設定

各スロットの `Config/Supermodel.ini` に以下を設定：

```ini
Streaming = 1
LinkPlay = 1   ; スロット番号（1〜4）
```

#### LinkPlay の値

| 値 | 説明 | 作成される仮想コントローラ数 |
|----|------|--------------------------|
| `0` | シングルタイトル用 — 1インスタンスでP1・P2をまとめてストリーミング | 2個（P1 + P2） |
| `1` | リンクプレイ スロット1（P1）/ シングルタイトル：P1ローカル＋P2ストリーミング | 1個 |
| `2` | リンクプレイ スロット2（P2） | 1個 |
| `3` | リンクプレイ スロット3（P3） | 1個 |
| `4` | リンクプレイ スロット4（P4） | 1個 |

> 2人プレイのシングルタイトルをストリーミングする場合は `LinkPlay = 0` を使用してください。クライアント側に仮想コントローラーが2つ作成されます。

> シングルタイトルでP1をホスト側でローカル操作する場合は `LinkPlay = 1`、`Streaming = 1` を設定してください。起動前にXInputコントローラーを接続しておいてください。

### 4. ROMを配置

ROMファイルをパッケージ直下の **`ROMs/`** フォルダに置いてください。各スロットのサンプル `Supermodel.ini` はすでにこのディレクトリを参照するよう設定されています。

### 5. supermodel.exe を起動

**`4lnkstart.bat`** を実行すると4インスタンスを一括起動できます。こちらの方法を推奨します。

> `4lnkstart with cmd.bat` はデバッグ用にコンソールウィンドウが開きます。

---

> ストリーミング用インスタンスは最小化しても構いません。Windowsのボリュームミキサーで音声をミュートしても問題ありません。ストリーミングの品質には影響しません。

クライアントは [StreamReceiver](https://github.com/BackPonBeauty/StreamReceiver) で接続してください。

---

## 🔧 設定

### 映像コーデックの選択（`Supermodel.ini`）

`Config/Supermodel.ini` を編集することで、ストリーミングに使用する映像コーデックを選択できます。

```ini
Decoder = H265   ; H.265 (HEVC) を使用 — 低ビットレートで高画質
Decoder = H264   ; H.264 (AVC) を使用  — 互換性重視（デフォルト）
```

| コーデック | 設定値 | 備考 |
|-----------|--------|------|
| H.264 (AVC) | `H264` | デフォルト。互換性が最も高い。 |
| H.265 (HEVC) | `H265` | 同等画質でビットレートを抑えられる。ホスト側に NVENC 対応 GPU が必要。 |

> **補足:** 設定ファイルのキー名は `Decoder` ですが、本来は `Codec` とすべきところでした。タイポです。

> ホストとクライアントの両方が選択したコーデックに対応している必要があります。  
> H.265 はホスト側に NVIDIA RTX 20 シリーズ以降の GPU が必要です。

### H.264 と H.265 の要件比較

| 役割 | H.264（デフォルト） | H.265 (HEVC) |
|------|-------------------|--------------|
| **ホスト側 GPU** | GTX 600 シリーズ以降（Kepler世代、2012年〜） | GTX 960 / 950 以上、または GTX 10シリーズ以降（Maxwell第2世代、2015/2016年〜） |
| **クライアント側 GPU/CPU** | ほぼすべてのPCで対応可能（2011年頃のPCでも可） | 第6世代 Intel CPU 以降、または GTX 960 / GTX 10シリーズ以降 |

H.264 は古くてスペックが低いPCでもほぼ確実に動くのが強みです。H.265 は「ここ10年以内に発売された標準的なPC」であれば、より少ない通信帯域で高画質・低遅延なプレイが可能になります。

> ⚠️ *上記の要件はAIによる情報をもとにしており、正確性を保証するものではありません。詳細はNVIDIA・Intelの公式ドキュメントをご確認ください。*

---

## 🖥️ クライアントアプリ

- [StreamReceiver](https://github.com/BackPonBeauty/StreamReceiver) — クライアントアプリ（映像受信・コントローラ送信）

---

## 🔧 関連リポジトリ

- [StreamReceiver](https://github.com/BackPonBeauty/StreamReceiver) — クライアントアプリ（映像受信・コントローラ送信）
- [Supermodel3-PonMi](https://github.com/BackPonBeauty/Supermodel3-PonMi) — ベースとなるPonMi editionエミュレータ

---

## ⚠️ Windows SmartScreen について

コード署名証明書が未取得のため、初回起動時に SmartScreen の警告が表示される場合があります。  
「詳細情報」→「実行」で起動できます。

---

## 📜 ライセンス

本プロジェクトは **GPL v3** ライセンスのもとで公開されています。  
オリジナルの [Supermodel3](https://www.supermodel3.com) に基づいています。

### サードパーティライブラリ

- ffmpeg `ffmpeg-2026-06-01-git-bf608f16fd-essentials_build` — [LGPL 2.1 / GPL 2.0](https://ffmpeg.org) / ビルド配布元：[gyan.dev](https://www.gyan.dev/ffmpeg/builds/)
- NVENC / CUDA — NVIDIA License
- [NVIDIA Video Codec SDK](https://developer.nvidia.com/nvidia-video-codec-sdk-license-agreement) — NVIDIA Toolkit License
- miniupnpc — BSD License
- ViGEm — BSD License
- Firebase C++ SDK — Apache 2.0

---

## 💬 コミュニティ

- [BackPonBeauty](https://github.com/BackPonBeauty) — GitHub
- [patreon.com/PonMi](https://patreon.com/PonMi) — Patreon
- [discord.gg/mNjPJHTTen](https://discord.gg/mNjPJHTTen) — Discord
- [back_pon_beauty](https://twitch.tv/back_pon_beauty) — Twitch
