/*
 * StreamClient.cpp
 *
 * Streaming (P5, 2026-11-16): the client. See StreamClient.h.
 */

#include "StreamClient.h"
#include "Protocol.h"
#include "DiscordAuth.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include "OSD/SDL/SDLIncludes.h"
#include <GL/glew.h>
#include "Pkgs/imgui/imgui.h"
#include "Pkgs/imgui/imgui_impl_sdl2.h"
#include "Pkgs/imgui/imgui_impl_opengl3.h"
#include <opus/opus.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace StreamProto;

namespace
{
  uint64_t NowMs() { return GetTickCount64(); }

  uint64_t NowUs()
  {
    static LARGE_INTEGER freq = []() { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (uint64_t) (c.QuadPart * 1000000 / freq.QuadPart);
  }

  SOCKET OpenClientSocket(int recvTimeoutMs)
  {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
      return s;
    BOOL newBehavior = FALSE;
    DWORD bytes = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &newBehavior, sizeof(newBehavior), nullptr, 0, &bytes, nullptr, nullptr);
    sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_port = 0;                    // the OS picks a free port (VF-03)
    local.sin_addr.s_addr = INADDR_ANY;
    bind(s, (sockaddr *) &local, sizeof(local));
    DWORD timeout = (DWORD) recvTimeoutMs;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *) &timeout, sizeof(timeout));
    int rcvbuf = 4 * 1024 * 1024;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char *) &rcvbuf, sizeof(rcvbuf));
    return s;
  }

  bool Resolve(const std::string &host, int port, sockaddr_in *out)
  {
    addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res)
      return false;
    *out = *(sockaddr_in *) res->ai_addr;
    out->sin_port = htons((u_short) port);
    freeaddrinfo(res);
    return true;
  }

  const char *RejectText(uint8_t reason)
  {
    switch (reason)
    {
    case R_VERSION:           return "The server runs a different version. Please update.";
    case R_NOT_MEMBER:        return "Refused: you are not a member of the Discord server.";
    case R_SLOT_BUSY:         return "That slot is in use. Please choose another slot.";
    case R_LOGIN_REQUIRED:    return "Refused: please log in to Discord (top of the lobby). Without login only this PC / home network can connect.";
    case R_CODEC:             return "Refused: this client cannot decode the server's video codec.";
    case R_LOGIN_UNAVAILABLE: return "Refused: the server could not check your Discord login now. Please try again later.";
    case R_LOGIN_INVALID:     return "Refused: your Discord login has expired. Please log out and log in again.";
    default:                  return "Refused by the server.";
    }
  }

  // Everything the threads share.
  struct Client
  {
    std::atomic<bool> running{true};
    std::string stopMessage;
    std::string serverNotice;   // last NOTICE text (shown when the server ends the session)
    std::mutex stopLock;

    SOCKET sessSock = INVALID_SOCKET, videoSock = INVALID_SOCKET, audioSock = INVALID_SOCKET;
    sockaddr_in sessAddr = {}, inputAddr = {}, videoAddr = {}, audioAddr = {};
    uint32_t key = 0;
    int streamW = 0, streamH = 0;
    bool h265 = false;
    int slot = 0;

    // session
    std::atomic<uint64_t> lastServerReply{0};
    std::atomic<int> rttUs{-1};
    std::atomic<uint64_t> lastKeyReq{0};

    // video
    std::mutex frameLock;
    std::vector<uint8_t> rgba;     // latest decoded frame
    int frameW = 0, frameH = 0;
    bool newFrame = false;
    std::atomic<uint32_t> framesDecoded{0};
    std::atomic<uint32_t> pktReceived{0}, pktLost{0};
    std::atomic<uint64_t> videoBytes{0};
    std::atomic<bool> gotVideo{false}, gotAudio{false};

    // audio
    SDL_AudioDeviceID audioDev = 0;

    // input
    std::mutex inputLock;
    Input input = {};
    uint16_t inputSeq = 0;

    void Stop(const std::string &msg)
    {
      std::lock_guard<std::mutex> lock(stopLock);
      if (stopMessage.empty())
        stopMessage = msg;
      running.store(false);
    }

    void SendSession(const void *data, int len)
    {
      sendto(sessSock, (const char *) data, len, 0, (const sockaddr *) &sessAddr, sizeof(sessAddr));
    }

    void RequestKeyframe(bool force = false)
    {
      uint64_t now = NowMs();
      if (!force && now - lastKeyReq.load() < 300)
        return;
      lastKeyReq.store(now);
      Header h;
      FillHeader(h, T_KEYREQ, key);
      SendSession(&h, sizeof(h));
    }

    // Caller holds inputLock.
    void SendInputLocked()
    {
      inputSeq++;
      FillHeader(input.h, T_INPUT, key);
      input.seq = inputSeq;
      sendto(sessSock, (const char *) &input, sizeof(input), 0, (const sockaddr *) &inputAddr, sizeof(inputAddr));
    }
  };

  // -------------------------------------------------------------------------------------------------------
  // Session thread: heartbeat, ping, status, media hello, server replies
  // -------------------------------------------------------------------------------------------------------
  void SessionThread(Client *c)
  {
    uint64_t lastHb = 0, lastPing = 0, lastStatus = 0, lastMedia = 0;
    uint32_t prevRecv = 0, prevLost = 0;
    char buf[2048];
    while (c->running.load())
    {
      uint64_t now = NowMs();
      if (now - lastHb >= kHeartbeatIntervalMs)
      {
        Header h;
        FillHeader(h, T_HB, c->key);
        c->SendSession(&h, sizeof(h));
        lastHb = now;
      }
      if (now - lastPing >= 500)
      {
        Ping p;
        FillHeader(p.h, T_PING, c->key);
        p.clientTime = NowUs();
        c->SendSession(&p, sizeof(p));
        lastPing = now;
      }
      if (now - lastStatus >= 1000)
      {
        uint32_t recv = c->pktReceived.load(), lost = c->pktLost.load();
        uint32_t dr = recv - prevRecv, dl = lost - prevLost;
        prevRecv = recv; prevLost = lost;
        Status s;
        FillHeader(s.h, T_STATUS, c->key);
        s.lossRate = (dr + dl) ? (float) dl / (float) (dr + dl) : 0.0f;
        c->SendSession(&s, sizeof(s));
        lastStatus = now;
      }
      // Media hello: often until both streams flow, then as a NAT keep-alive
      bool flowing = c->gotVideo.load() && c->gotAudio.load();
      if (now - lastMedia >= (flowing ? 5000u : 1000u))
      {
        Header m;
        FillHeader(m, T_MEDIA_HELLO, c->key);
        sendto(c->videoSock, (const char *) &m, sizeof(m), 0, (const sockaddr *) &c->videoAddr, sizeof(c->videoAddr));
        sendto(c->audioSock, (const char *) &m, sizeof(m), 0, (const sockaddr *) &c->audioAddr, sizeof(c->audioAddr));
        if (!c->gotVideo.load())
          c->RequestKeyframe(true);
        lastMedia = now;
      }
      // The server is gone?
      if (now - c->lastServerReply.load() > kDropTimeoutMs)
      {
        c->Stop("The server stopped answering (no reply for 6 s).");
        break;
      }

      sockaddr_in from = {};
      int fromLen = sizeof(from);
      int n = recvfrom(c->sessSock, buf, sizeof(buf), 0, (sockaddr *) &from, &fromLen);
      if (n <= 0)
        continue;
      Header h;
      if (!ParseHeader(buf, n, &h) || h.version != kVersion || h.key != c->key)
        continue;
      c->lastServerReply.store(NowMs());
      if (h.type == T_PONG && n >= (int) sizeof(Ping))
      {
        Ping p;
        memcpy(&p, buf, sizeof(p));
        c->rttUs.store((int) (NowUs() - p.clientTime));
      }
      else if (h.type == T_NOTICE && n >= (int) sizeof(Notice))
      {
        Notice nt;
        memcpy(&nt, buf, sizeof(nt));
        nt.text[sizeof(nt.text) - 1] = '\0';
        c->serverNotice = nt.text;
      }
      else if (h.type == T_BYE)
      {
        c->Stop(c->serverNotice.empty() ? "The server ended the session." : c->serverNotice);
        break;
      }
    }
  }

  // -------------------------------------------------------------------------------------------------------
  // Video thread: RTP -> Annex-B frames -> decode -> RGBA
  // -------------------------------------------------------------------------------------------------------
  bool HasIrap(const std::vector<uint8_t> &f, bool h265)
  {
    for (size_t i = 0; i + 3 < f.size(); i++)
    {
      if (f[i] == 0 && f[i + 1] == 0 && f[i + 2] == 1)
      {
        uint8_t b = f[i + 3];
        if (h265) { int t = (b >> 1) & 0x3F; if (t >= 16 && t <= 21) return true; }
        else      { if ((b & 0x1F) == 5) return true; }
      }
    }
    return false;
  }

  void VideoThread(Client *c)
  {
    const AVCodec *codec = avcodec_find_decoder(c->h265 ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);
    AVCodecContext *ctx = codec ? avcodec_alloc_context3(codec) : nullptr;
    if (!ctx)
    {
      c->Stop("No video decoder in this build.");
      return;
    }
    ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    ctx->thread_count = 1;
    if (avcodec_open2(ctx, codec, nullptr) < 0)
    {
      avcodec_free_context(&ctx);
      c->Stop("Could not open the video decoder.");
      return;
    }
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    SwsContext *sws = nullptr;
    std::vector<uint8_t> rgba;

    std::vector<uint8_t> au;          // current access unit (Annex-B)
    bool auBroken = false;
    bool needKeyframe = true;
    bool haveSeq = false;
    uint16_t expectedSeq = 0;
    static const uint8_t startCode[4] = { 0, 0, 0, 1 };
    std::vector<uint8_t> buf(65536);

    while (c->running.load())
    {
      int n = recv(c->videoSock, (char *) buf.data(), (int) buf.size(), 0);
      if (n < 13)
        continue;
      const uint8_t *p = buf.data();
      if ((p[0] & 0xC0) != 0x80)
        continue;   // not RTP v2
      c->gotVideo.store(true);
      c->videoBytes += (uint64_t) n;
      uint16_t seq = (uint16_t) ((p[2] << 8) | p[3]);
      bool marker = (p[1] & 0x80) != 0;

      if (haveSeq && seq != expectedSeq)
      {
        uint16_t gap = (uint16_t) (seq - expectedSeq);
        if (gap < 0x8000)
        {
          c->pktLost += gap;
          auBroken = true;
          needKeyframe = true;
          c->RequestKeyframe();
        }
        else
          continue;   // late / duplicate packet
      }
      haveSeq = true;
      expectedSeq = (uint16_t) (seq + 1);
      c->pktReceived++;

      const uint8_t *pl = p + 12;
      int plen = n - 12;
      if (c->h265)
      {
        int type = (pl[0] >> 1) & 0x3F;
        if (type == 49 && plen >= 3)
        {
          bool s = (pl[2] & 0x80) != 0;
          if (s)
          {
            au.insert(au.end(), startCode, startCode + 4);
            au.push_back((uint8_t) ((pl[0] & 0x81) | ((pl[2] & 0x3F) << 1)));
            au.push_back(pl[1]);
          }
          au.insert(au.end(), pl + 3, pl + plen);
        }
        else
        {
          au.insert(au.end(), startCode, startCode + 4);
          au.insert(au.end(), pl, pl + plen);
        }
      }
      else
      {
        int type = pl[0] & 0x1F;
        if (type == 28 && plen >= 2)
        {
          bool s = (pl[1] & 0x80) != 0;
          if (s)
          {
            au.insert(au.end(), startCode, startCode + 4);
            au.push_back((uint8_t) ((pl[0] & 0xE0) | (pl[1] & 0x1F)));
          }
          au.insert(au.end(), pl + 2, pl + plen);
        }
        else
        {
          au.insert(au.end(), startCode, startCode + 4);
          au.insert(au.end(), pl, pl + plen);
        }
      }

      if (!marker)
        continue;

      // One whole frame
      bool decodeIt = !auBroken;
      if (decodeIt && needKeyframe)
      {
        if (HasIrap(au, c->h265))
          needKeyframe = false;
        else
        {
          decodeIt = false;
          c->RequestKeyframe();
        }
      }
      if (decodeIt)
      {
        size_t size = au.size();
        au.resize(size + AV_INPUT_BUFFER_PADDING_SIZE, 0);
        pkt->data = au.data();
        pkt->size = (int) size;
        if (avcodec_send_packet(ctx, pkt) >= 0)
        {
          while (avcodec_receive_frame(ctx, frame) >= 0)
          {
            int w = frame->width, h = frame->height;
            sws = sws_getCachedContext(sws, w, h, (AVPixelFormat) frame->format, w, h, AV_PIX_FMT_RGBA,
                                       SWS_POINT, nullptr, nullptr, nullptr);
            rgba.resize((size_t) w * h * 4);
            uint8_t *dst[4] = { rgba.data(), nullptr, nullptr, nullptr };
            int dstStride[4] = { w * 4, 0, 0, 0 };
            sws_scale(sws, frame->data, frame->linesize, 0, h, dst, dstStride);
            {
              std::lock_guard<std::mutex> lock(c->frameLock);
              c->rgba.swap(rgba);
              c->frameW = w;
              c->frameH = h;
              c->newFrame = true;
            }
            c->framesDecoded++;
          }
        }
        else
        {
          needKeyframe = true;
          c->RequestKeyframe();
        }
      }
      au.clear();
      auBroken = false;
    }

    if (sws) sws_freeContext(sws);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
  }

  // -------------------------------------------------------------------------------------------------------
  // Audio thread: 4-byte timestamp + Opus -> SDL queue
  // -------------------------------------------------------------------------------------------------------
  void AudioThread(Client *c)
  {
    int err = 0;
    OpusDecoder *dec = opus_decoder_create(48000, 2, &err);
    if (!dec)
      return;
    std::vector<uint8_t> buf(4096);
    std::vector<int16_t> pcm(48000 / 50 * 2 * 3);
    const Uint32 maxQueued = 48000 * 2 * 2 * 120 / 1000;   // keep at most ~120 ms queued
    while (c->running.load())
    {
      int n = recv(c->audioSock, (char *) buf.data(), (int) buf.size(), 0);
      if (n <= 4)
        continue;
      c->gotAudio.store(true);
      int frames = opus_decode(dec, buf.data() + 4, n - 4, pcm.data(), (int) pcm.size() / 2, 0);
      if (frames <= 0 || !c->audioDev)
        continue;
      if (SDL_GetQueuedAudioSize(c->audioDev) > maxQueued)
        SDL_ClearQueuedAudio(c->audioDev);
      SDL_QueueAudio(c->audioDev, pcm.data(), (Uint32) (frames * 2 * sizeof(int16_t)));
    }
    opus_decoder_destroy(dec);
  }

  // -------------------------------------------------------------------------------------------------------
  // Input thread: the latest state every 8 ms
  // -------------------------------------------------------------------------------------------------------
  void InputThread(Client *c)
  {
    while (c->running.load())
    {
      {
        std::lock_guard<std::mutex> lock(c->inputLock);
        c->SendInputLocked();
      }
      Sleep(kInputIntervalMs);
    }
  }

  // -------------------------------------------------------------------------------------------------------
  // Handshake (before the window opens)
  // -------------------------------------------------------------------------------------------------------
  bool Handshake(Client *c, const StreamConnectRequest &req, std::string &message)
  {
    Hello hello = {};
    FillHeader(hello.h, T_HELLO, 0);
    hello.codecs = CODEC_H264 | CODEC_H265;
    hello.login = LOGIN_NONE;
    memcpy(hello.name, req.name.c_str(), std::min(req.name.size(), (size_t) kMaxNameLength));   // zero padded, no terminator needed
    hello.tokenLen = 0;

    // P6b (CL-07, SV-16): with a Discord login the access token follows the Hello struct; the host checks it
    std::string token;
    if (DiscordAuth::GetAccessToken(token) && token.size() <= 256)
    {
      hello.login = LOGIN_DISCORD;
      hello.tokenLen = (uint16_t) token.size();
    }
    std::vector<char> packet(sizeof(hello) + token.size());
    memcpy(packet.data(), &hello, sizeof(hello));
    if (hello.tokenLen)
      memcpy(packet.data() + sizeof(hello), token.data(), token.size());
    else
      packet.resize(sizeof(hello));

    char buf[512];
    uint64_t start = NowMs();
    uint64_t lastSend = 0;
    const uint64_t waitMs = hello.tokenLen ? 9000 : 5000;   // the host asks Discord (up to 5 s)
    while (NowMs() - start < waitMs)
    {
      if (NowMs() - lastSend >= 500)
      {
        c->SendSession(packet.data(), (int) packet.size());
        lastSend = NowMs();
      }
      sockaddr_in from = {};
      int fromLen = sizeof(from);
      int n = recvfrom(c->sessSock, buf, sizeof(buf), 0, (sockaddr *) &from, &fromLen);
      if (n <= 0)
        continue;
      Header h;
      if (!ParseHeader(buf, n, &h))
        continue;
      if (h.type == T_REJECT && n >= (int) sizeof(Reject))
      {
        message = RejectText(((Reject *) buf)->reason);
        return false;
      }
      if (h.type == T_WELCOME && n >= (int) sizeof(Welcome))
      {
        Welcome w;
        memcpy(&w, buf, sizeof(w));
        c->key = w.h.key;
        c->streamW = w.width;
        c->streamH = w.height;
        c->h265 = (w.codec == CODEC_H265);
        c->slot = w.slot;
        return true;
      }
    }
    message = "No answer from " + req.host + " (slot " + std::to_string(req.slot) + "). Is the server running?";
    return false;
  }
}

// ---------------------------------------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------------------------------------

StreamClientExit RunStreamClient(const StreamConnectRequest &req, std::string &message)
{
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);

  Client client;
  Client *c = &client;
  int base = SlotBasePort(req.slot);
  if (!Resolve(req.host, base + PORT_SESSION, &c->sessAddr))
  {
    message = "Cannot find the host \"" + req.host + "\".";
    return StreamClientExit::BackToLobby;
  }
  c->inputAddr = c->videoAddr = c->audioAddr = c->sessAddr;
  c->inputAddr.sin_port = htons((u_short) (base + PORT_INPUT));
  c->videoAddr.sin_port = htons((u_short) (base + PORT_VIDEO));
  c->audioAddr.sin_port = htons((u_short) (base + PORT_AUDIO));

  c->sessSock = OpenClientSocket(100);
  c->videoSock = OpenClientSocket(100);
  c->audioSock = OpenClientSocket(100);
  auto closeSockets = [c]() {
    if (c->sessSock != INVALID_SOCKET) closesocket(c->sessSock);
    if (c->videoSock != INVALID_SOCKET) closesocket(c->videoSock);
    if (c->audioSock != INVALID_SOCKET) closesocket(c->audioSock);
  };

  printf("[Client] Connecting to %s slot %d (session port %d) as \"%s\"\n", req.host.c_str(), req.slot, base + PORT_SESSION, req.name.c_str());
  if (!Handshake(c, req, message))
  {
    printf("[Client] %s\n", message.c_str());
    closeSockets();
    return StreamClientExit::BackToLobby;
  }
  printf("[Client] Connected: %dx%d %s, slot %d\n", c->streamW, c->streamH, c->h265 ? "H265" : "H264", c->slot);
  c->lastServerReply.store(NowMs());

  // --- SDL window, GL, ImGui, audio, pad ---
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");   // IN-05: the pad also in the background
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK);
  SDL_Window *window = SDL_CreateWindow("Supermodel PonMi - Spikeout streaming", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                        960, 540, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
  SDL_GLContext gl = SDL_GL_CreateContext(window);
  SDL_GL_SetSwapInterval(0);   // lowest latency
  glewExperimental = GL_TRUE;
  glewInit();

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ImGui_ImplSDL2_InitForOpenGL(window, gl);
  ImGui_ImplOpenGL3_Init("#version 150");

  SDL_AudioSpec want = {}, have = {};
  want.freq = 48000;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 480;
  c->audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (c->audioDev)
    SDL_PauseAudioDevice(c->audioDev, 0);

  SDL_Joystick *joy = SDL_NumJoysticks() > 0 ? SDL_JoystickOpen(0) : nullptr;

  GLuint tex = 0;
  int texW = 0, texH = 0;
  bool fullscreen = false, showStats = false;
  StreamClientExit exitKind = StreamClientExit::BackToLobby;

  std::thread tSession(SessionThread, c);
  std::thread tVideo(VideoThread, c);
  std::thread tAudio(AudioThread, c);
  std::thread tInput(InputThread, c);

  uint64_t lastRender = 0;
  uint32_t fpsFrames = 0;
  uint64_t fpsStart = NowMs();
  float fps = 0.0f, kbps = 0.0f;
  uint64_t lastBytes = 0;

  while (c->running.load())
  {
    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
      ImGui_ImplSDL2_ProcessEvent(&e);
      if (e.type == SDL_QUIT)
      {
        exitKind = StreamClientExit::QuitApp;
        c->Stop("");
      }
      else if (e.type == SDL_KEYDOWN && !e.key.repeat)
      {
        SDL_Keycode k = e.key.keysym.sym;
        if (k == SDLK_F11)
        {
          fullscreen = !fullscreen;
          SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        }
        else if (k == SDLK_F3)
          showStats = !showStats;
        else if (k == SDLK_ESCAPE)
        {
          if (fullscreen)
          {
            fullscreen = false;
            SDL_SetWindowFullscreen(window, 0);
          }
          else
            c->Stop("You left the session.");
        }
      }
      else if (e.type == SDL_JOYDEVICEADDED && !joy)
        joy = SDL_JoystickOpen(e.jdevice.which);
      else if (e.type == SDL_JOYDEVICEREMOVED && joy && e.jdevice.which == SDL_JoystickInstanceID(joy))
      {
        SDL_JoystickClose(joy);
        joy = SDL_NumJoysticks() > 0 ? SDL_JoystickOpen(0) : nullptr;
      }
    }

    // --- Input state ---
    Input in = {};
    bool focused = (SDL_GetKeyboardFocus() == window);
    if (focused)
    {
      const Uint8 *ks = SDL_GetKeyboardState(nullptr);
      if (ks[SDL_SCANCODE_W]) in.keys |= 0x0001;
      if (ks[SDL_SCANCODE_A]) in.keys |= 0x0002;
      if (ks[SDL_SCANCODE_S]) in.keys |= 0x0004;
      if (ks[SDL_SCANCODE_D]) in.keys |= 0x0008;
      if (ks[SDL_SCANCODE_UP])   in.meta |= META_BITRATE_UP;
      if (ks[SDL_SCANCODE_DOWN]) in.meta |= META_BITRATE_DOWN;
    }
    if (joy)
    {
      in.padConnected = 1;
      int nb = SDL_JoystickNumButtons(joy);
      for (int b = 0; b < nb && b < 32; b++)
        if (SDL_JoystickGetButton(joy, b))
          in.buttons |= (1u << b);
      int na = SDL_JoystickNumAxes(joy);
      for (int a = 0; a < na && a < 6; a++)
        in.axes[a] = SDL_JoystickGetAxis(joy, a);
      if (SDL_JoystickNumHats(joy) > 0)
        in.hat = SDL_JoystickGetHat(joy, 0);
    }
    {
      std::lock_guard<std::mutex> lock(c->inputLock);
      bool changed = in.keys != c->input.keys || in.meta != c->input.meta || in.padConnected != c->input.padConnected ||
                     in.buttons != c->input.buttons || in.hat != c->input.hat || memcmp(in.axes, c->input.axes, sizeof(in.axes)) != 0;
      if (changed)
      {
        c->input.keys = in.keys;
        c->input.meta = in.meta;
        c->input.padConnected = in.padConnected;
        c->input.buttons = in.buttons;
        memcpy(c->input.axes, in.axes, sizeof(in.axes));
        c->input.hat = in.hat;
        c->SendInputLocked();   // send at once, don't wait for the 8 ms tick
      }
    }

    // --- New video frame -> texture ---
    bool gotFrame = false;
    {
      std::lock_guard<std::mutex> lock(c->frameLock);
      if (c->newFrame)
      {
        if (!tex || texW != c->frameW || texH != c->frameH)
        {
          if (tex) glDeleteTextures(1, &tex);
          glGenTextures(1, &tex);
          glBindTexture(GL_TEXTURE_2D, tex);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
          glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, c->frameW, c->frameH, 0, GL_RGBA, GL_UNSIGNED_BYTE, c->rgba.data());
          texW = c->frameW;
          texH = c->frameH;
        }
        else
        {
          glBindTexture(GL_TEXTURE_2D, tex);
          glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texW, texH, GL_RGBA, GL_UNSIGNED_BYTE, c->rgba.data());
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        c->newFrame = false;
        gotFrame = true;
        fpsFrames++;
      }
    }

    uint64_t now = NowMs();
    if (now - fpsStart >= 1000)
    {
      fps = fpsFrames * 1000.0f / (float) (now - fpsStart);
      uint64_t bytes = c->videoBytes.load();
      kbps = (float) (bytes - lastBytes) * 8.0f / (float) (now - fpsStart);
      lastBytes = bytes;
      fpsFrames = 0;
      fpsStart = now;
    }

    // --- Draw (on a new frame, or at least ~30 times a second for the overlay and messages) ---
    if (gotFrame || now - lastRender >= 33)
    {
      lastRender = now;
      int ww, wh;
      SDL_GL_GetDrawableSize(window, &ww, &wh);
      glViewport(0, 0, ww, wh);
      glClearColor(0, 0, 0, 1);
      glClear(GL_COLOR_BUFFER_BIT);

      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplSDL2_NewFrame();
      ImGui::NewFrame();
      if (tex)
      {
        // Keep the stream's aspect ratio, centered (CL-08). The encoder sends the GL frame bottom-up -> flip.
        float aspect = (float) texW / (float) texH;
        float dw = (float) ww, dh = dw / aspect;
        if (dh > wh) { dh = (float) wh; dw = dh * aspect; }
        float x0 = (ww - dw) * 0.5f, y0 = (wh - dh) * 0.5f;
        ImGui::GetBackgroundDrawList()->AddImage((ImTextureID) (intptr_t) tex, ImVec2(x0, y0), ImVec2(x0 + dw, y0 + dh),
                                                 ImVec2(0, 1), ImVec2(1, 0));
      }
      else
      {
        ImGui::GetBackgroundDrawList()->AddText(ImVec2(20, 20), IM_COL32(255, 255, 255, 255), "Connected. Waiting for video...");
      }
      if (showStats)
      {
        ImGui::SetNextWindowPos(ImVec2(8, 8));
        ImGui::SetNextWindowBgAlpha(0.55f);
        ImGui::Begin("##stats", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                         ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
        int rtt = c->rttUs.load();
        if (rtt >= 0) ImGui::Text("Latency (round trip): %.1f ms", rtt / 1000.0f);
        else          ImGui::Text("Latency (round trip): ---");
        uint32_t rcv = c->pktReceived.load(), lost = c->pktLost.load();
        ImGui::Text("Video: %.0f fps  %.0f kbps  %s %dx%d", fps, kbps, c->h265 ? "H265" : "H264", texW, texH);
        ImGui::Text("Lost packets: %u / %u", lost, rcv + lost);
        ImGui::Text("Slot %d", c->slot);
        ImGui::End();
      }
      ImGui::Render();
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
      SDL_GL_SwapWindow(window);
    }
    else
      SDL_Delay(1);
  }

  // --- Leave ---
  {
    Header bye;
    FillHeader(bye, T_BYE, c->key);
    c->SendSession(&bye, sizeof(bye));
  }
  c->running.store(false);
  tSession.join();
  tVideo.join();
  tAudio.join();
  tInput.join();
  closeSockets();

  if (exitKind == StreamClientExit::BackToLobby)
  {
    std::lock_guard<std::mutex> lock(c->stopLock);
    message = c->stopMessage;
  }
  printf("[Client] Session ended%s%s\n", message.empty() ? "" : ": ", message.c_str());

  if (joy) SDL_JoystickClose(joy);
  if (c->audioDev) SDL_CloseAudioDevice(c->audioDev);
  if (tex) glDeleteTextures(1, &tex);
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_GL_DeleteContext(gl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return exitKind;
}
