# Supermodel3-PonMi-Streaming

A **WAN remote play streaming fork** of the Sega Model 3 emulator [Supermodel3](https://www.supermodel3.com).

Up to 4 players can enjoy link play over the internet.

---

## Features

- **Up to 4-link support** (mix of local and remote players over WAN, up to 4 players)
- **Low-latency video streaming** (NVENC H.264 encoding)
- **Audio streaming** (Opus 128kbps)
- **XInput controller input via UDP**
- **Firebase-based matchmaking** (automatic host discovery)
- **UPnP automatic port forwarding** support
- **Built-in UI/Launcher** (No external helper tools needed to configure slots or host play!)

---

## Confirmed Working Titles

<img width="962" height="572" alt="image" src="https://github.com/user-attachments/assets/687436f5-0c9f-4ae9-8fb1-97d1a261725d" />
<img width="962" height="572" alt="image" src="https://github.com/user-attachments/assets/decdc046-8931-4693-8cb5-cee51f7e4f0e" />

- Spikeout Final Edition (spikeofe)

---

## Related Repositories

The following client-side application is required to connect and play.

| App | Description | Link |
|-----|-------------|------|
| **StreamReceiver** | Client application (video reception & controller input) | [BackPonBeauty/StreamReceiver](https://github.com/BackPonBeauty/StreamReceiver)  |

> Releases include **ffmpeg** binaries.
> ffmpeg is licensed under LGPL/GPL. Source code is available at https://ffmpeg.org

---

<img width="398" height="490" alt="名称未設定-2" src="https://github.com/user-attachments/assets/c4bba737-7d27-4692-befc-ee7fb3aeabe2" />

## Requirements

### Host

| Item | Requirement |
|------|-------------|
| OS | Windows 10/11 64-bit |
| GPU | NVIDIA RTX 20 series or later (NVENC required) |
| Driver | CUDA 13.0 compatible or later |
| Other | [ViGEmBus](https://github.com/nefarius/ViGEm.Bus) driver installed |
| Router | UPnP enabled (or manual port forwarding) |
| Network | 10 Mbps upload or faster recommended |

### Client

| Item | Requirement |
|------|-------------|
| OS | Windows 10/11 64-bit |
| Router | UPnP enabled (or manual port forwarding) |
| Network | 5 Mbps download or faster recommended |

---

## Port Numbers

| Slot | XInput | HS/HB | Video | Audio |
|------|--------|-------|-------|-------|
| P1 | 5000 | 5001 | 5002 | 5003 |
| P2 | 5004 | 5005 | 5006 | 5007 |
| P3 | 5008 | 5009 | 5010 | 5011 |
| P4 | 5012 | 5013 | 5014 | 5015 |

---

## Setup (Host)

### 1. Install ViGEmBus

Download and install the latest release from [ViGEmBus Releases](https://github.com/nefarius/ViGEm.Bus/releases). This driver is required to create virtual controllers on the host machine.

### 2. Folder Structure

Configure your directory as follows:

```
Supermodel3-PonMi-Streaming/
├── bin64/
│   ├── supermodel.exe  (Built emulator & UI launcher)
│   ├── Resolution.txt
│   ├── Snaps/ (Screenshots directory)
│   └── Config/
│       └── Supermodel.ini
```

### 3. Launching and Hosting

1. Launch `supermodel.exe` to open the GUI launcher.
2. Select your ROM Directory at the bottom (`DIR...` button) and select your game from the list.
3. Configure your graphics and audio settings under the **Video** and **Sound** tabs.
4. Go to the **Network** tab:
   - Check **Network** to enable link play.
   - Check **Streaming** to enable the WAN remote play stream server (this automatically initializes the ViGEm virtual controller and registers the instance on Firebase).
   - Configure **PortIn**, **PortOut**, and **AddressOut** as necessary.
5. Click **LAUNCH** to start hosting. The host registration and matchmaking mapping to Firebase will happen automatically.

---

## Setup (Client)

### How to Connect

<img width="494" height="512" alt="名称未設定-1" src="https://github.com/user-attachments/assets/78289230-71a1-4bd5-bd12-f4cc486fb9ee" />

1. Launch `StreamReceiver.exe`
2. Select a host from the list (retrieved automatically via Firebase)
3. Choose an available slot (green ●) and press **Connect**
4. Play begins when video appears

**Key Controls:**
- `Escape` : Disconnect and return to host selection
- `F11` : Toggle full-screen (Patron edition only)

---

## License

This software is distributed under the GPL license.

The following libraries are used:

- **Supermodel3** - GPL / https://www.supermodel3.com
- **ffmpeg (essentials build by gyan.dev)** - LGPL/GPL / https://ffmpeg.org | https://www.gyan.dev/ffmpeg/builds/
- **Nefarius.ViGEm.Client** - MIT / https://github.com/nefarius/ViGEm.Client
- **Mono.NAT** - MIT / https://github.com/lontivero/Open.NAT
- **Firebase.Database.NET** - MIT / https://github.com/step-up-labs/firebase-database-dotnet

---

## Author

**背中ポン美 (BackPonBeauty)**

- YouTube: [@BackPonBeauty](https://www.youtube.com/@BackPonBeauty)
- GitHub: [BackPonBeauty](https://github.com/BackPonBeauty)
- X: [@BackPonBeauty](https://x.com/BackPonBeauty)
