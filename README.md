# 🎮 Supermodel3-PonMi-Streaming-ffmpeg — Spikeout FE 8-link & WAN streaming

<img width="1920" height="1080" alt="vlcsnap-2026-11-14-16h07m11s076" src="https://github.com/user-attachments/assets/9f0bce05-759c-4869-9e6d-71a7ff5e107f" />
<img width="1920" height="1080" alt="vlcsnap-2026-11-14-14h18m37s350" src="https://github.com/user-attachments/assets/e3c38231-1e39-4c26-86ed-c8ce4c81f9ec" />


**Start a host in the launcher, pick a slot in the lobby, and play.**
**StreamReceiver, XinputReceiver and ViGEmBus are no longer needed.**

> Build 20261116 / PonLuncher 2.0.0

---

## 🆕 What's new

- **Spikeout Final Edition with up to 8 players** (2–8 links). Bots can play the cabinets nobody is using
- **The client is the same supermodel.exe** — no StreamReceiver. Open the lobby from the setup screen and connect
- **New launcher, PonLuncher** — choose the number of links, the mode of each slot (stream, local, bot), the codec and so on, then press START
- **Discord sign-in** — only members of the designated Discord server can host and join
- **Entry tickets** — your Discord token never goes to the host. A small server checks your membership and gives you a signed, single-use ticket for that host and slot
- **Firebase lobby** — hosts and free slots are listed automatically. No IP address to type
- **Game progress in the lobby and on Discord** — each running game is shown as stage, area and number of players (GAME A, GAME B, …)
- **Discord status post** — the host's free and used slots are posted to a Discord channel and kept up to date (Slot 1–4 and Slot 5–8 in two columns)
- **Automatic port forwarding with UPnP** — in most homes no router setup is needed
- **NVENC (NVIDIA) without the CUDA Toolkit** — the NVIDIA driver is enough. On other GPUs the stream is encoded with FFmpeg (x264 / x265, CPU)
- **H.264 / H.265** video, Opus audio
- **Anti-abuse** — disconnect after 3 minutes without input, host-side kick, one client per PC, one slot per Discord account, old versions cannot connect

### Spikeout FE changes

- **5–8 players:** players 5–8 get their own costume colours (made from your own ROM when the game starts; no pictures are shipped), their own name colours on the HUD, and the KO / PK counters and ally stats for 8 players
- **Joining a running game:** a player who joins always joins **GAME A**, the game already in progress
- **Time limit:** with 5 or more players in a game, the in-game countdown is held at 99. With 4 or fewer it runs as in the original
- **3-minute mode is blocked:** pressing START while holding Shift + Beat + Charge + Jump does nothing
- **Enemy textures:** when someone joins, enemy textures that could not be loaded are loaded again (no more black enemies until the next area)
- **Bots** can tell humans from bots through the link itself (no helper files). Their character is chosen in the launcher (BOT CHAR), and they can pick a random character when they continue

---

## ✅ Tested titles

- **Spikeout Final Edition** (spikeofe) — 2–8 links; 8 players over the internet confirmed

---

## ⚙️ Specifications and limits

- **One player per slot** (no spectators). A slot in use cannot be joined
- **When a player disconnects, their character stands still** (it does not become a bot). The next person who joins that slot takes control
- **3 minutes without input disconnects you automatically** (a warning is shown for the last 30 seconds)
- **Connecting over the internet requires Discord sign-in.** Without sign-in you can only connect from the same PC or your home network
- **The client needs the ROM too** (the lobby opens when Spikeout FE is selected)
- **Connect your controllers before starting** so Windows recognises them (controllers connected later are not detected)
- The client uses the first controller
- Streaming 8 slots at once puts a heavy load on the host. For 8 links a lower resolution (960×540 or 640×360) is recommended (tested: 8 links at 640×360 on an RTX 3060 host)
- The room password option is turned off in the launcher for now (all rooms are public)

---

## 📦 Package contents (host)

```
spikeout8/
├── host/
│   ├── PonLuncher.exe          ← launcher (start here)
│   ├── supermodel.exe
│   ├── Config/
│   │   ├── Supermodel_01.ini   ← slot 1 (… Supermodel_08.ini)
│   │   └── …
│   ├── NVRAM/
│   ├── Roms/                   ← put your ROM files here
│   └── Assets/
└── waypoints.txt               ← routes for the bots
```

A client only needs the same `supermodel.exe`, `Config/Supermodel.ini` and `Roms/`.

> ⚠️ ROM files are not included. Use ROMs you own.

PonLuncher is distributed as a program only; its source is not part of this repository.

---

## 💻 Requirements

### Host

| Item | Requirement |
|------|-------------|
| OS | Windows 10 / 11, 64-bit |
| CPU | Intel Core i7 or AMD Ryzen 7 or better (single-thread speed matters) |
| GPU | NVIDIA (NVENC) recommended. Other GPUs stream with FFmpeg (CPU) |
| Discord | Member of the designated Discord server |
| Router | UPnP enabled (or forward the ports by hand) |
| Connection | 20 Mbps upload or more recommended (more players need more) |

### Client

| Item | Requirement |
|------|-------------|
| OS | Windows 10 / 11, 64-bit |
| Discord | Member of the designated Discord server (over the internet) |
| Other | The ROM file, an XInput or DirectInput controller (a keyboard also works) |

> Test machine: Core i7-13700F / RTX 4070 Super 12GB / 64GB RAM / Windows 11 (hosting also tested on an RTX 3060 PC)

---

## 🔌 Ports (UDP)

Slot n uses 4 ports starting at `55000 + (n − 1) × 4`.

| Slot | Input | Session | Video | Audio |
|------|-------|---------|-------|-------|
| 1 | 55000 | 55001 | 55002 | 55003 |
| 2 | 55004 | 55005 | 55006 | 55007 |
| 3 | 55008 | 55009 | 55010 | 55011 |
| 4 | 55012 | 55013 | 55014 | 55015 |
| 5 | 55016 | 55017 | 55018 | 55019 |
| 6 | 55020 | 55021 | 55022 | 55023 |
| 7 | 55024 | 55025 | 55026 | 55027 |
| 8 | 55028 | 55029 | 55030 | 55031 |

---

## 🚀 Hosting

### 1. Allow it in Windows Firewall

Allow `supermodel.exe` on both private and public networks.

### 2. Add the ROM

Put the ROM file (`spikeofe.zip` etc.) in `host/Roms/`.

### 3. Start PonLuncher

1. **DISCORD**: press LOGIN and sign in to Discord in your browser. "server member" is shown if you are a member of the server
2. **HOST**:
   - HOST NAME: the name shown in the lobby
   - GAME: spikeofe
   - LINKS: number of links (2–8)
   - CODEC: H264 or H265
   - ENCODER: AUTO (NVENC on NVIDIA, FFmpeg otherwise) / NVENC / FFMPEG
   - BOT CHAR: the bots' character — ORIGINAL (the cursor's character), SPIKE, LINDA, TENSHIN or WHITE
   - RANDOM CHARACTER ON CONTINUE: with ORIGINAL, bots pick a random character when they continue
3. **SLOTS**: choose a mode for each slot

| Mode | Meaning |
|------|---------|
| STREAM | A slot for a player over the internet (streamed) |
| LOCAL | A slot played with a controller on the host PC (choose XINPUT / DINPUT under PAD) |
| BOT | A slot played by a bot |

4. **START** launches every slot and lists the host in the lobby. **STOP** ends it
5. The name of each player is shown under PLAYER. **KICK** removes them from that slot

> The streaming windows can be minimised. This does not affect the stream.

---

## 🕹️ Playing (client)

1. Start `supermodel.exe`, select **Spikeout Final Edition** on the setup screen and press **LAUNCH**
2. The lobby opens. Press LOGIN under **Discord** at the top
3. In the host list, press a **green slot (free)**
4. Once connected, the game screen appears

| Key | Action |
|-----|--------|
| F11 | Full screen |
| F3 | Show latency and other stats |
| ↑ / ↓ | Raise / lower the bitrate |
| ESC | Full screen → window → back to the lobby |

> A host on the same PC or in your home network can also be reached with "Connect directly" at the bottom of the lobby, by typing its IP address.

---

## 🔧 Settings

### Video codec

Chosen with CODEC in the launcher (`Codec = H264` / `Codec = H265` in the ini).

| Codec | Value | Notes |
|-------|-------|-------|
| H.264 (AVC) | `H264` | Default. Light and runs on any PC |
| H.265 (HEVC) | `H265` | Lower bitrate for the same quality. Slightly more CPU load on the client |

> The client decodes the video on the CPU, so any client GPU works.

### Encoder

| Value | Meaning |
|-------|---------|
| `AUTO` | NVENC if there is an NVIDIA GPU, FFmpeg otherwise |
| `NVENC` | Stream with the NVIDIA GPU (low latency; no CUDA Toolkit, the driver is enough) |
| `FFMPEG` | Stream with the CPU (x264 / x265). Works with any GPU |

---

## 🛠️ Building from source

To build supermodel.exe yourself, see [Docs/BUILD_FFMPEG.md](Docs/BUILD_FFMPEG.md).

---

## ⚠️ Windows SmartScreen

The program has no code-signing certificate, so SmartScreen may warn you the first time you run it.
Click **"More info" → "Run anyway"** to start it.

---

## 📜 License

This software is distributed under the **GNU General Public License version 3 (GPL v3)**. The full text is in [LICENSE](LICENSE).

- It is based on [Supermodel3](https://github.com/trzy/Supermodel), which is GPL v3, so this modified program as a whole is GPL v3 as well.
- The FFmpeg (`--enable-gpl`), x264 and x265 it contains are GPL too, and the distributed exe is under the GPL. Versions and build steps: [Docs/BUILD_FFMPEG.md](Docs/BUILD_FFMPEG.md).
- Anyone may use, modify and redistribute it. When you distribute it (modified or not), provide the source under GPL v3 too.
- The licenses of the libraries are listed below and in [PonMi_LICENSE.txt](PonMi_LICENSE.txt). Their full license texts are in [THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt) (include it when you distribute the exe).
- No game ROM is included.
- **PonLuncher.exe** (the launcher) is a separate program and is not covered by the GPL: it only writes the ini files and starts supermodel.exe with command-line options. It is freeware. You may use it and redistribute it free of charge, unmodified, together with this package. Its source is not published, and it comes with no warranty.

```
Supermodel3-PonMi
Copyright (C) 2026 BackPonBeauty

Based on Supermodel3 (https://github.com/trzy/Supermodel)
Copyright (C) 2003-2026 The Supermodel Team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
```

## Libraries

This software uses the following libraries (~~struck through~~ = no longer used in this version).

### [Firebase](https://firebase.google.com/)
- Realtime Database and Authentication through their REST API (no SDK)
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
- Only the CUDA driver API header (`cuda.h`) at build time. Not needed to run (the CUDA runtime is not used)
- [License: CUDA Toolkit EULA](https://docs.nvidia.com/cuda/eula/index.html)
- Copyright (c) 2020 NVIDIA Corporation. All rights reserved.

### [NVIDIA Video Codec SDK](https://developer.nvidia.com/nvidia-video-codec-sdk)
- Only the header `nvEncodeAPI.h` is included in the source (that file is MIT-licensed). Encoding uses the GPU driver's `nvEncodeAPI64.dll`, loaded at run time
- License: MIT License (at the top of `Src/Video/nvEncodeAPI.h`)
- Copyright (c) 2010-2024 NVIDIA Corporation

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
- This build includes x264 / x265, so it is GPL (`--enable-gpl`)
- Version: `master` commit `a5e6c01` (2026-07-15, `8.0.git`). Build steps: [Docs/BUILD_FFMPEG.md](Docs/BUILD_FFMPEG.md)
- [License: GPL ver.2 or later (this build) / LGPL ver.2.1 or later (FFmpeg itself)](https://www.ffmpeg.org/legal.html)
- Copyright (c) 2000-2026 the FFmpeg developers

### [x264](https://www.videolan.org/developers/x264.html) (new)
- H.264 software encoding (FFMPEG encoder)
- Version: 0.165.r3222.b35605a (MSYS2 package, static library)
- [License: GPL ver.2 or later](https://code.videolan.org/videolan/x264/-/blob/master/COPYING)
- Copyright (c) the x264 project

### [x265](https://www.videolan.org/developers/x265.html) (new)
- H.265 software encoding (FFMPEG encoder)
- Version: 4.1 (MSYS2 package, static library)
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

### [Minizip](https://github.com/madler/zlib/tree/master/contrib/minizip) (new)
- Reads the ROM zip files
- License: zlib License
- Copyright (c) 1998-2010 Gilles Vollant

### [Dear ImGui](https://github.com/ocornut/imgui)
- [License: MIT License](https://github.com/ocornut/imgui/blob/master/LICENSE.txt)
- Copyright (c) 2014-2026 Omar Cornut

### [TinyXML-2](https://github.com/leethomason/tinyxml2)
- [License: zlib License](https://github.com/leethomason/tinyxml2/blob/master/LICENSE.txt)
- Copyright (c) 2011-2026 Lee Thomason

### [GLEW](https://glew.sourceforge.net/) (new)
- [License: Modified BSD License / MIT License](https://github.com/nigels-com/glew/blob/master/LICENSE.txt)
- Copyright (c) the GLEW authors (Milan Ikits, Marcelo Magallon, Nigel Stewart and others)

### [minimp3](https://github.com/lieff/minimp3) (new)
- [License: CC0 1.0](https://github.com/lieff/minimp3/blob/master/LICENSE)
- lieff

### [libiconv](https://www.gnu.org/software/libiconv/) (new)
- [License: LGPL ver.2.1 or later](https://www.gnu.org/software/libiconv/)
- Copyright (c) Free Software Foundation, Inc.

### [Opus](https://www.opus-codec.org/)
- [License: BSD 3-Clause License](https://github.com/xiph/opus/blob/master/COPYING)
- Copyright (c) 2001-2026 Xiph.Org Foundation, Skype Limited, Octasic, Jean-Marc Valin, Timothy B. Terriberry, CSIRO, Gregory Maxwell, Mark Borgerding, Erik de Castro Lopo

### [Musashi M68K Emulator](https://github.com/kstenerud/Musashi)
- Used under the GPL with the permission of Karl Stenerud (see `Src/CPU/68K/Musashi`)
- Copyright (c) Karl Stenerud

### Services used (new)
- [Discord](https://discord.com/): sign-in (OAuth2 / PKCE), server membership check, slot status posts through a webhook — [Discord Developer Terms of Service](https://discord.com/developers/docs/policies-and-agreements/developer-terms-of-service)
- [Cloudflare Workers](https://workers.cloudflare.com/): the entry-ticket server (checks Discord membership and signs the ticket)

---

## 💬 Community

- [BackPonBeauty](https://github.com/BackPonBeauty) — GitHub
- [patreon.com/PonMi](https://patreon.com/PonMi) — Patreon
- [discord.com/invite/zpKBFDA52f](https://discord.com/invite/zpKBFDA52f) — Discord
- [back_pon_beauty](https://twitch.tv/back_pon_beauty) — Twitch
