/*
 * Lobby.cpp
 *
 * Streaming (P5, 2026-11-16): lobby screen. See Lobby.h.
 */

#include "Protocol.h"   // winsock2.h first (before windows.h from the Firebase header)
#include "Lobby.h"
#include "Secrets.h"
#include "DiscordAuth.h"
#include "Version.h"
#include "Remote/FirebaseMatchingCpp.h"
#include <shellapi.h>
#include "Pkgs/imgui/imgui.h"
#include "OSD/SDL/SDLIncludes.h"
#include "OSD/SDL/PonUI.h"
#include <algorithm>
#include <map>
#include <ws2tcpip.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <filesystem>

namespace Lobby
{
  static std::atomic<bool> s_open{false};
  static bool s_loaded = false;
  static std::string s_message;
  static StreamConnectRequest s_request;

  static char s_hostBuf[128] = "127.0.0.1";
  static char s_name[StreamProto::kMaxNameLength + 1] = "guest";
  static int  s_slot = 1;
  // 11/19 (owner): the pad to send (see Lobby.h PadIndex / PadName)
  static int  s_padIndex = 0;
  static std::string s_padName;

  static const char *kSettingsFile = "streamclient.ini";

  // SV-18: the password box (a modal popup). The typed password stays in memory only.
  static bool s_pwOpenRequest = false;
  static bool s_pwWrong = false;
  static bool s_pwFocus = false;
  static char s_pwBuf[StreamProto::kMaxPasswordLength + 1] = {};
  static StreamConnectRequest s_pwReq;

  // --- Host list from Firebase (P6, LB-01/02/03) ---
  static std::mutex s_listLock;
  static std::vector<LobbyHostCpp> s_hosts;
  static std::string s_listStatus;
  static std::string s_myExternalIp, s_myLanIp;
  static std::atomic<bool> s_refresh{true};
  static std::atomic<bool> s_listThreadStarted{false};

  // 11/16: version check (Stream/Version.h). Guarded by s_listLock.
  static bool s_updateRequired = false;
  static long long s_newestBuild = 0;
  static std::string s_updateUrl;

  // 11/24 (owner): NETLINK plays from your own ROM - the GUI tells us where it is; checked before joining as NETLINK
  static std::string s_romFile;
  static StreamConnectRequest s_pickReq;    // a REMOTE slot was clicked: STREAM or NETLINK? (the box in Draw)
  static bool s_pickLocked = false;
  static bool s_pickOpenRequest = false;

  void SetRomFile(const std::string &path) { s_romFile = path; }

  static bool HaveRom()
  {
    static Uint32 last = 0;
    static bool have = false;
    Uint32 now = SDL_GetTicks();
    if (last == 0 || now - last > 1000)   // once a second is enough
    {
      std::error_code ec;
      have = !s_romFile.empty() && std::filesystem::is_regular_file(std::filesystem::u8path(s_romFile), ec);
      last = now ? now : 1;
    }
    return have;
  }

  static std::string NoRomText()
  {
    return "NETLINK NEEDS THE ROM: " + (s_romFile.empty() ? std::string("Roms\\spikeofe.zip") : s_romFile) + " WAS NOT FOUND.";
  }

  static void SetListStatus(const std::string &s)
  {
    std::lock_guard<std::mutex> lock(s_listLock);
    s_listStatus = s;
  }

  static std::string ChooseAddress(const LobbyHostCpp &h);

  // 11/24 (owner): ping to each host in the list - one PING (key 0 = a probe, see SessionServer) to the session port of
  // its first slot, at the address CONNECT would use; the PONG echoes our time. Up to 1 s; no answer = -1 (an older host,
  // or blocked). Keyed by the host's Firebase key. Guarded by s_listLock.
  static std::map<std::string, int> s_pingMs;
  static void MeasurePings(const std::vector<LobbyHostCpp> &hosts)
  {
    using namespace StreamProto;
    static bool wsa = false;
    if (!wsa) { WSADATA wd; wsa = WSAStartup(MAKEWORD(2, 2), &wd) == 0; }
    std::map<std::string, int> result;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
      return;
    struct Target { std::string key; sockaddr_in addr; };
    std::vector<Target> targets;
    LARGE_INTEGER freq; QueryPerformanceFrequency(&freq);
    auto nowUs = [&]() { LARGE_INTEGER c; QueryPerformanceCounter(&c); return (uint64_t) (c.QuadPart * 1000000 / freq.QuadPart); };
    for (const auto &h : hosts)
    {
      result[h.key] = -1;
      int slot = 0;
      for (int k = 1; k <= 8 && !slot; k++)
        if (h.slots[k].present) slot = k;
      if (!slot) continue;
      sockaddr_in a = {};
      a.sin_family = AF_INET;
      a.sin_port = htons((u_short) (SlotBasePort(slot) + PORT_SESSION));
      if (inet_pton(AF_INET, ChooseAddress(h).c_str(), &a.sin_addr) != 1) continue;
      Ping p = {};
      FillHeader(p.h, T_PING, 0);
      p.clientTime = nowUs();
      sendto(s, (const char *) &p, sizeof(p), 0, (const sockaddr *) &a, sizeof(a));
      targets.push_back({ h.key, a });
    }
    const uint64_t t0 = GetTickCount64();
    size_t got = 0;
    while (got < targets.size() && GetTickCount64() - t0 < 1000)
    {
      fd_set rf; FD_ZERO(&rf); FD_SET(s, &rf);
      timeval tv = { 0, 50000 };
      if (select(0, &rf, nullptr, nullptr, &tv) <= 0) continue;
      char buf[256]; sockaddr_in from = {}; int fl = sizeof(from);
      const int n = recvfrom(s, buf, sizeof(buf), 0, (sockaddr *) &from, &fl);
      Header hh;
      if (n < (int) sizeof(Ping) || !ParseHeader(buf, n, &hh) || hh.type != T_PONG) continue;
      Ping p; memcpy(&p, buf, sizeof(p));
      for (const auto &t : targets)
        if (t.addr.sin_addr.s_addr == from.sin_addr.s_addr && t.addr.sin_port == from.sin_port && result[t.key] < 0)
        {
          const uint64_t rtt = nowUs() - p.clientTime;
          result[t.key] = (int) std::min<uint64_t>(rtt / 1000, 9999);
          got++;
          break;
        }
    }
    closesocket(s);
    std::lock_guard<std::mutex> lock(s_listLock);
    s_pingMs = result;
  }

  // Background: sign in once, then read /hosts every 10 s (or on REFRESH) while the lobby is open.
  static void ListThread()
  {
    std::string ext = FirebaseMatchingCpp::GetExternalIp();
    std::string lan = FirebaseMatchingCpp::GetLanIp();
    {
      std::lock_guard<std::mutex> lock(s_listLock);
      s_myExternalIp = ext;
      s_myLanIp = lan;
    }
    FirebaseMatchingCpp fb;
    bool signedIn = false;
    uint64_t last = 0;
    for (;;)
    {
      uint64_t now = GetTickCount64();
      if (!s_open.load() || !(s_refresh.exchange(false) || now - last >= 10000))
      {
        Sleep(100);
        continue;
      }
      last = now;
      if (!signedIn)
      {
        SetListStatus("Signing in to the lobby...");
        signedIn = fb.Initialize();
        if (!signedIn)
        {
          SetListStatus("Lobby unavailable (Firebase sign-in failed). Direct connection still works.");
          continue;
        }
      }
      // 11/16: newest build published by the owner (/main5). Older than that -> no connecting (update first).
      long long newest = 0;
      std::string url;
      if (fb.FetchMain5Version(newest, url))
      {
        std::lock_guard<std::mutex> lock(s_listLock);
        s_newestBuild = newest;
        s_updateUrl = url.empty() ? StreamVersion::kDefaultDownloadUrl : url;
        s_updateRequired = newest > StreamVersion::kBuild;
      }
      std::vector<LobbyHostCpp> hosts;
      bool ok = fb.FetchHosts(hosts);
      if (ok)
        MeasurePings(hosts);   // 11/24: the round trip to each host (shown after its name)
      std::lock_guard<std::mutex> lock(s_listLock);
      if (ok)
      {
        s_hosts = hosts;
        s_listStatus = std::to_string(hosts.size()) + (hosts.size() == 1 ? " host" : " hosts") + " online (updated every 10 s)";
      }
      else
        s_listStatus = "Could not read the host list. Retrying...";
    }
  }

  // Logged in: the Discord name (the host replaces it with the checked one anyway). Else the typed name.
  static std::string RequestName()
  {
    if (DiscordAuth::GetState() == DiscordAuth::State::LoggedIn)
    {
      std::string n = DiscordAuth::UserName();
      if (!n.empty())
        return n;
    }
    return s_name[0] ? s_name : "guest";
  }

  // Same router as the host (same external IP): use its LAN address, or this PC itself.
  static std::string ChooseAddress(const LobbyHostCpp &h)
  {
    std::lock_guard<std::mutex> lock(s_listLock);
    if (!s_myExternalIp.empty() && h.ip == s_myExternalIp)
    {
      if (h.lanip.empty() || h.lanip == s_myLanIp)
        return "127.0.0.1";
      return h.lanip;
    }
    return h.ip;
  }

  static void Load()
  {
    if (s_loaded)
      return;
    s_loaded = true;
    std::ifstream ifs(kSettingsFile);
    std::string line;
    while (std::getline(ifs, line))
    {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      size_t eq = line.find('=');
      if (eq == std::string::npos)
        continue;
      std::string key = line.substr(0, eq), val = line.substr(eq + 1);
      while (!key.empty() && key.back() == ' ') key.pop_back();
      while (!val.empty() && val.front() == ' ') val.erase(0, 1);
      if (key == "StreamLastHost")       snprintf(s_hostBuf, sizeof(s_hostBuf), "%s", val.c_str());
      else if (key == "StreamGuestName") snprintf(s_name, sizeof(s_name), "%s", val.c_str());
      else if (key == "StreamLastSlot")  { int v = atoi(val.c_str()); if (v >= 1 && v <= 8) s_slot = v; }
      else if (key == "StreamPad")       { int v = atoi(val.c_str()); if (v >= 1 && v <= 16) s_padIndex = v - 1; }
      else if (key == "StreamPadName")   s_padName = val;
    }
  }

  static void Save()
  {
    std::ofstream ofs(kSettingsFile, std::ios::trunc);
    if (!ofs.is_open())
      return;
    ofs << "; Streaming client lobby settings (Supermodel PonMi main5)\n";
    ofs << "StreamLastHost = " << s_hostBuf << "\n";
    ofs << "StreamLastSlot = " << s_slot << "\n";
    ofs << "StreamGuestName = " << s_name << "\n";
    ofs << "StreamPad = " << (s_padIndex + 1) << "\n";
    ofs << "StreamPadName = " << s_padName << "\n";
  }

  int PadIndex()
  {
    Load();
    return s_padIndex;
  }

  std::string PadName()
  {
    Load();
    return s_padName;
  }

  // 11/19 (owner): the "Pad" row - the connected pads (SDL joysticks); the choice is saved at once.
  // 11/24 (owner): PonNvram's look - a pill "< 1: XBOX 360 CONTROLLER >"
  static void DrawPadRow(float scale, float labelW)
  {
    int n = SDL_NumJoysticks();
    if (n < 0)
      n = 0;
    std::vector<std::string> names;
    for (int i = 0; i < n; i++)
    {
      const char *nm = SDL_JoystickNameForIndex(i);
      names.push_back(nm ? nm : "");
    }
    // which one is chosen now: the saved name first, else the saved number
    int cur = -1;
    if (!s_padName.empty())
      for (int i = 0; i < n && cur < 0; i++)
        if (names[i] == s_padName)
          cur = i;
    if (cur < 0 && s_padName.empty() && s_padIndex < n)
      cur = s_padIndex;
    std::vector<std::string> items;
    for (int i = 0; i < n; i++)
      items.push_back(std::to_string(i + 1) + ": " + names[i]);
    int pick = cur;
    if (cur < 0)   // the saved pad is not here: show that first (choosing it does nothing)
    {
      if (n == 0)
        items.insert(items.begin(), "No pad connected");
      else if (!s_padName.empty())
        items.insert(items.begin(), s_padName + " (not connected - pad 1 is used)");
      else
        items.insert(items.begin(), "Pad " + std::to_string(s_padIndex + 1) + " (not connected - pad 1 is used)");
      pick = 0;
    }

    PonUI::Label("PAD", labelW - 8.0f);
    ImGui::SameLine(labelW);
    if (PonUI::PillChoice("##pad", &pick, items, 520.0f * scale))
    {
      const int i = cur < 0 ? pick - 1 : pick;
      if (i >= 0 && i < n)
      {
        s_padIndex = i;
        s_padName = names[i];
        Save();
      }
    }
    ImGui::SameLine();
    PonUI::Label("SENT TO THE HOST WHILE CONNECTED", ImGui::GetContentRegionAvail().x, PonUI::cFaint);
  }

  void Open(const std::string &message)
  {
    Load();
    s_open = true;
    s_message = message;
    s_refresh = true;
    if (StreamSecrets::HasFirebase() && !s_listThreadStarted.exchange(true))
      std::thread(ListThread).detach();
  }

  void Close()
  {
    s_open = false;
  }

  bool IsOpen()
  {
    return s_open;
  }

  StreamConnectRequest &Request()
  {
    return s_request;
  }

  void AskPassword(const StreamConnectRequest &req, bool wrong)
  {
    s_pwReq = req;
    s_pwReq.requested = false;
    s_pwReq.password.clear();
    s_pwWrong = wrong;
    s_pwOpenRequest = true;
    memset(s_pwBuf, 0, sizeof(s_pwBuf));
  }

  // SV-18: a small padlock drawn with lines (no icon font needed), one text line high
  static void DrawLock(ImU32 col)
  {
    float size = ImGui::GetTextLineHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float w = size * 0.62f, h = size * 0.50f;
    float x0 = p.x + (size - w) * 0.5f, y0 = p.y + size - h - size * 0.04f;
    float r = w * 0.30f, thick = size * 0.12f;
    ImVec2 c(x0 + w * 0.5f, y0 - size * 0.02f);
    dl->PathArcTo(c, r, 3.14159265f, 2.0f * 3.14159265f, 12);
    dl->PathStroke(col, 0, thick);
    dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x - r, y0 + 1.0f), col, thick);
    dl->AddLine(ImVec2(c.x + r, c.y), ImVec2(c.x + r, y0 + 1.0f), col, thick);
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), col, size * 0.08f);
    ImGui::Dummy(ImVec2(size, size));
  }

  // 11/24 (owner): PonNvram's look (PonUI): the game's font, pills, orange / cyan / green / red
  Action Draw(float scale)
  {
    Load();
    Action result = Action::None;

    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f * scale, 16.0f * scale));
    ImGui::Begin("##Lobby", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);   // 11/24: fits the window
    ImGui::PopStyleVar();
    ImGui::SetWindowFontScale(scale);
    const int sc = PonUI::Scale();
    const float labelW = 200.0f * scale;

    // 11/16: an older build than the one published in Firebase may not connect (playing locally still works)
    bool updateRequired;
    long long newestBuild;
    std::string updateUrl;
    {
      std::lock_guard<std::mutex> lock(s_listLock);
      updateRequired = s_updateRequired;
      newestBuild = s_newestBuild;
      updateUrl = s_updateUrl;
    }

    // --- header: SPIKEOUT FE (big), STREAMING LOBBY, and the Discord badge at the top right ---
    {
      const ImVec2 top = ImGui::GetCursorScreenPos();
      PonUI::Title("SPIKEOUT FE", PonUI::cText, sc + 2);
      ImGui::SameLine(0, 20.0f * scale);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f * (sc + 2) - 8.0f * sc - 2.0f);
      PonUI::Title("ONLINE LOBBY", PonUI::cAccent, sc);   // 11/24 (owner): was STREAMING LOBBY

      // Discord (P6b, CL-07): needed to join hosts over the internet
      DiscordAuth::State st = DiscordAuth::GetState();
      const char *btn = st == DiscordAuth::State::LoggedIn ? "LOGOUT" : st == DiscordAuth::State::Waiting ? "CANCEL" : "LOGIN";
      // 11/24 (owner): logged in -> the Discord name alone (no "Logged in as")
      std::string who = st == DiscordAuth::State::LoggedIn ? DiscordAuth::UserName() : DiscordAuth::StateText();
      if (who.empty())
        who = DiscordAuth::StateText();
      const ImVec2 whoSz = ImGui::CalcTextSize(who.c_str());
      const float btnW = PonUI::Measure(btn, std::max(1, sc - 1)).x + 18.0f;
      const float badgeW = whoSz.x + 28.0f * scale;
      const float h = ImGui::GetFrameHeight();
      const float right = top.x + ImGui::GetContentRegionAvail().x;
      const ImVec2 b0(right - btnW - 10.0f * scale - badgeW, top.y + 4.0f * scale);
      ImDrawList *dl = ImGui::GetWindowDrawList();
      const ImU32 frame = st == DiscordAuth::State::LoggedIn ? IM_COL32(88, 101, 242, 255)   // Discord's blurple
                        : st == DiscordAuth::State::Failed   ? PonUI::cError : IM_COL32(255, 170, 60, 120);
      dl->AddRectFilled(b0, ImVec2(b0.x + badgeW, b0.y + h), IM_COL32(30, 37, 56, 255), h * 0.3f);
      dl->AddRect(b0, ImVec2(b0.x + badgeW, b0.y + h), frame, h * 0.3f, 0, std::max(1.5f, 2.0f * scale));
      dl->AddText(ImVec2(b0.x + (badgeW - whoSz.x) * 0.5f, b0.y + (h - whoSz.y) * 0.5f),
                  st == DiscordAuth::State::LoggedIn ? PonUI::cText : PonUI::cDim, who.c_str());
      const ImVec2 back = ImGui::GetCursorScreenPos();
      ImGui::SetCursorScreenPos(ImVec2(right - btnW, b0.y));
      if (PonUI::SmallButton(btn, st == DiscordAuth::State::LoggedIn ? PonUI::Kind::Normal : PonUI::Kind::Primary))
      {
        if (st == DiscordAuth::State::LoggedIn)
          DiscordAuth::Logout();
        else if (st == DiscordAuth::State::Waiting)
          DiscordAuth::CancelLogin();
        else
          DiscordAuth::StartLogin();
      }
      ImGui::SetCursorScreenPos(back);
    }
    PonUI::Separator();
    ImGui::Spacing();

    if (!s_message.empty())
      PonUI::Text(s_message.c_str(), PonUI::cWarn);
    if (updateRequired)
    {
      char buf[160];
      snprintf(buf, sizeof(buf), "THIS SUPERMODEL IS TOO OLD (BUILD %lld, NEWEST %lld). PLEASE UPDATE TO CONNECT.", StreamVersion::kBuild, newestBuild);
      PonUI::Label(buf, ImGui::GetContentRegionAvail().x - 200.0f * scale, PonUI::cError);
      ImGui::SameLine();
      if (PonUI::SmallButton("DOWNLOAD PAGE", PonUI::Kind::Primary))
        ShellExecuteA(nullptr, "open", updateUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    else
      PonUI::TextF(PonUI::cFaint, "BUILD %lld", StreamVersion::kBuild);
    ImGui::Spacing();

    DrawPadRow(scale, labelW);
    ImGui::Spacing();

    // --- Host list (P6, LB-01..03): click a free slot to connect ---
    PonUI::Label("HOSTS", labelW - 8.0f, PonUI::cAccent);
    if (!StreamSecrets::HasFirebase())
    {
      ImGui::SameLine(labelW);
      PonUI::Label("HOST LIST IS OFF: THIS BUILD HAS NO FIREBASE KEYS. DIRECT CONNECTION STILL WORKS.", ImGui::GetContentRegionAvail().x, PonUI::cDim);
    }
    else
    {
      std::vector<LobbyHostCpp> hosts;
      std::string status;
      std::map<std::string, int> pings;
      {
        std::lock_guard<std::mutex> lock(s_listLock);
        hosts = s_hosts;
        pings = s_pingMs;
        status = s_listStatus;
      }
      ImGui::SameLine(labelW);
      if (PonUI::SmallButton("REFRESH"))
        s_refresh = true;
      ImGui::SameLine(0, 12.0f * scale);
      PonUI::Label(status.c_str(), ImGui::GetContentRegionAvail().x, PonUI::cDim);

      const float slotW = 30.0f * scale, slotGap = 4.0f * scale;
      // 11/24: the list fills the window down to the footer (EXIT and the key line sit at the bottom)
      float listH = std::max(120.0f * scale, ImGui::GetContentRegionAvail().y - (46.0f + 34.0f) * scale - 8.0f * sc);
      ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f * scale, 6.0f * scale));
      if (ImGui::BeginTable("##hosts", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                            ImVec2(0, listH)))
      {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("SERVER", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("GAME", ImGuiTableColumnFlags_WidthFixed, 120.0f * scale);
        ImGui::TableSetupColumn("SLOTS", ImGuiTableColumnFlags_WidthFixed, 8 * (slotW + slotGap) + 8.0f);
        // the header row in the game font
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        const char *heads[] = { "SERVER", "GAME", "SLOTS (CLICK A FREE ONE)" };
        for (int c = 0; c < 3; c++)
        {
          ImGui::TableSetColumnIndex(c);
          PonUI::Label(heads[c], ImGui::GetContentRegionAvail().x, PonUI::cDim);
        }
        for (size_t i = 0; i < hosts.size(); i++)
        {
          const LobbyHostCpp &h = hosts[i];
          // 11/24 (owner): the server name bigger, and the row's parts centred on one line height
          const float rowH = ImGui::GetFrameHeight() + 14.0f * scale;
          const float padY = (rowH - ImGui::GetFrameHeight()) * 0.5f;
          ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
          ImGui::TableSetColumnIndex(0);
          const char *shownName = h.servername.empty() ? h.ip.c_str() : h.servername.c_str();
          {
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            float x = p0.x;
            if (h.locked)
            {
              ImGui::SetCursorScreenPos(ImVec2(x, p0.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f));
              DrawLock(IM_COL32(255, 200, 60, 255));
              if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Password required");
              x += ImGui::GetTextLineHeight() + 6.0f * scale;
            }
            // 11/24 (owner): the server name in a Windows font (PonUI::SystemFont)
            ImFont *nameFont = PonUI::SystemFont();
            const float nameSize = ImGui::GetFontSize() * 1.35f;
            const ImVec2 nsz = nameFont->CalcTextSizeA(nameSize, FLT_MAX, 0.0f, shownName);
            ImGui::GetWindowDrawList()->AddText(nameFont, nameSize, ImVec2(x, p0.y + (rowH - nsz.y) * 0.5f), PonUI::cText, shownName);
            // 11/24 (owner): the ping after the name (game font; green / yellow / red; "-" = no answer yet / older host)
            float endX = x + nsz.x;
            {
              auto it = pings.find(h.key);
              const int ms = it == pings.end() ? -1 : it->second;
              char pb[24];
              if (ms >= 0) snprintf(pb, sizeof(pb), "%dMS", ms); else snprintf(pb, sizeof(pb), "-");
              const ImU32 pc = ms < 0 ? PonUI::cDim : ms < 60 ? IM_COL32(90, 230, 140, 255) : ms < 120 ? IM_COL32(255, 196, 64, 255) : IM_COL32(255, 92, 92, 255);
              const float px0 = endX + 14.0f * scale;
              PonUI::Draw(ImGui::GetWindowDrawList(), ImVec2(px0, p0.y + (rowH - 8.0f * sc) * 0.5f), pb, sc, pc);
              endX = px0 + PonUI::Measure(pb, sc).x;
            }
            ImGui::SetCursorScreenPos(p0);
            ImGui::Dummy(ImVec2(endX - p0.x, rowH));
          }
          // 11/17 (owner): the games running on this host ("stage,area,players;..." from the host, GAME A first)
          if (!h.sess.empty())
          {
            size_t p = 0;
            for (int g = 0; p <= h.sess.size() && g < 8; g++)
            {
              size_t e = h.sess.find(';', p);
              std::string part = h.sess.substr(p, e == std::string::npos ? std::string::npos : e - p);
              unsigned st = 0, ar = 0, pl = 0;
              if (sscanf(part.c_str(), "%u,%u,%u", &st, &ar, &pl) == 3)
                ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "GAME %c：STAGE %u・エリア%u・%u人", 'A' + g, st, ar, pl);
              if (e == std::string::npos)
                break;
              p = e + 1;
            }
          }
          ImGui::TableSetColumnIndex(1);
          ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padY);
          PonUI::Label(h.gametitle.c_str(), ImGui::GetContentRegionAvail().x, PonUI::cLabel);
          ImGui::TableSetColumnIndex(2);
          ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padY);
          for (int s = 1; s <= 8; s++)
          {
            if (s > 1)
              ImGui::SameLine(0, slotGap);
            ImGui::PushID((int) (i * 16 + s));
            char label[8];
            snprintf(label, sizeof(label), "%d", s);
            const LobbyHostCpp::Slot &sl = h.slots[s];
            const ImVec2 bs(slotW, ImGui::GetFrameHeight());
            if (!sl.present)
              PonUI::Button("-", bs, PonUI::Kind::Normal, false);
            else if (sl.available)
            {
              // 11/23 netlink: a NETLINK slot (played from your own ROM) is blue, a video slot green
              // 11/24: a REMOTE slot (either) is cyan - clicking asks STREAM or NETLINK
              bool clicked = PonUI::Button(label, bs, sl.remote ? PonUI::Kind::Primary : sl.netlink ? PonUI::Kind::Netlink : PonUI::Kind::Go, !updateRequired);
              if (clicked && !updateRequired)
              {
                std::string addr = ChooseAddress(h);
                snprintf(s_hostBuf, sizeof(s_hostBuf), "%s", addr.c_str());
                s_slot = s;
                Save();
                StreamConnectRequest req;
                req.host = addr;
                req.slot = s;
                req.name = RequestName();
                req.serverName = shownName;
                req.netlink = sl.netlink;
                if (sl.remote)
                {
                  s_pickReq = req;
                  s_pickLocked = h.locked;
                  s_pickOpenRequest = true;
                }
                else if (sl.netlink && !HaveRom())
                  s_message = NoRomText();
                else if (h.locked)
                  AskPassword(req, false);   // SV-18: the password box first
                else
                {
                  s_request = req;
                  s_request.requested = true;
                  result = Action::Connect;
                }
              }
              if (ImGui::IsItemHovered())
              {
                if (sl.remote)
                  ImGui::SetTooltip(h.locked ? "Slot %d: free - choose STREAM (video) or NETLINK (your own ROM) (password required)"
                                             : "Slot %d: free - choose STREAM (video) or NETLINK (your own ROM)", s);
                else if (sl.netlink)
                  ImGui::SetTooltip(h.locked ? "Slot %d: free NETLINK slot - plays from your own ROM (password required)"
                                             : "Slot %d: free NETLINK slot - plays from your own ROM", s);
                else
                  ImGui::SetTooltip(h.locked ? "Slot %d: free - click to join (password required)" : "Slot %d: free - click to join", s);
              }
            }
            else
            {
              const ImVec2 p0 = ImGui::GetCursorScreenPos();
              ImGui::InvisibleButton("##used", bs);
              ImDrawList *dl = ImGui::GetWindowDrawList();
              dl->AddRectFilled(p0, ImVec2(p0.x + bs.x, p0.y + bs.y), IM_COL32(110, 40, 46, 255), std::min(10.0f, bs.y * 0.25f));
              const ImVec2 tsz = PonUI::Measure(label, sc);
              PonUI::Draw(dl, ImVec2(p0.x + (bs.x - tsz.x) * 0.5f, p0.y + (bs.y - tsz.y) * 0.5f), label, sc, IM_COL32(200, 140, 140, 255));
              if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Slot %d: in use%s%s", s, sl.player.empty() ? "" : " by ", sl.player.c_str());
            }
            ImGui::PopID();
          }
        }
        ImGui::EndTable();
      }
      ImGui::PopStyleVar();
    }
    PonUI::Separator();
    ImGui::Spacing();

    // 11/24 (owner): CONNECT DIRECTLY removed (host IP / slot / name / netlink and its CONNECT) - join from the host list.
    // A guest's name is the one kept in streamclient.ini (or "guest"); logged in, the Discord name.
    const float btnH = 46.0f * scale;
    // 11/24 (owner): EXIT and the line under it stick to the bottom of the window
    {
      const float hintH = 8.0f * sc;
      const float footY = ImGui::GetWindowHeight() - 22.0f * scale - btnH - ImGui::GetStyle().ItemSpacing.y * 2.0f - hintH;
      if (footY > ImGui::GetCursorPosY())
        ImGui::SetCursorPosY(footY);
    }
    // 11/24 (owner): SKIP (PLAY LOCALLY) removed - the lobby only joins hosts (Action::Skip is no longer returned)
    // 11/24 (owner): BACK -> EXIT, closes the program (was: back to the settings screen)
    if (PonUI::Button("EXIT", ImVec2(140.0f * scale, btnH), PonUI::Kind::Stop))
    {
      Save();
      result = Action::Back;
    }

    ImGui::Spacing();
    PonUI::Label("WHILE CONNECTED:  F11 FULLSCREEN,  F3 LATENCY,  UP/DOWN BITRATE,  ESC LEAVE (BACK HERE)",
                 ImGui::GetContentRegionAvail().x, PonUI::cFaint);

    // --- 11/24 (owner): a REMOTE slot - join as STREAM (video from the host) or NETLINK (your own ROM) ---
    if (s_pickOpenRequest)
    {
      s_pickOpenRequest = false;
      ImGui::OpenPopup("JoinAs##remote");
    }
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f * scale, 16.0f * scale));
    if (ImGui::BeginPopupModal("JoinAs##remote", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar))
    {
      ImGui::SetWindowFontScale(scale);
      ImGui::Text("%s  -  slot %d", s_pickReq.serverName.empty() ? s_pickReq.host.c_str() : s_pickReq.serverName.c_str(), s_pickReq.slot);
      ImGui::Spacing();
      PonUI::Text("JOIN AS", PonUI::cLabel);
      ImGui::Spacing();
      const bool rom = HaveRom();
      const ImVec2 pb(270.0f * scale, 44.0f * scale);
      bool asStream = PonUI::Button("STREAM (VIDEO)", pb, PonUI::Kind::Go, !updateRequired);
      ImGui::SameLine(0, 10.0f * scale);
      bool asNetlink = PonUI::Button("NETLINK (YOUR ROM)", pb, PonUI::Kind::Netlink, rom && !updateRequired);
      if (!rom)
        PonUI::Text(NoRomText().c_str(), PonUI::cWarn);
      ImGui::Spacing();
      bool cancel = PonUI::Button("CANCEL", ImVec2(150.0f * scale, 40.0f * scale), PonUI::Kind::Normal);
      if ((asStream || (asNetlink && rom)) && !updateRequired)
      {
        StreamConnectRequest req = s_pickReq;
        req.netlink = asNetlink && rom;   // the HELLO says it: codecs = 0 for netlink, the codecs for video
        ImGui::CloseCurrentPopup();
        if (s_pickLocked)
          AskPassword(req, false);
        else
        {
          s_request = req;
          s_request.requested = true;
          result = Action::Connect;
        }
      }
      else if (cancel)
        ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
    ImGui::PopStyleVar();

    // --- SV-18: password box ---
    if (s_pwOpenRequest)
    {
      s_pwOpenRequest = false;
      s_pwFocus = true;
      ImGui::OpenPopup("Password##room");
    }
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f * scale, 16.0f * scale));
    if (ImGui::BeginPopupModal("Password##room", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar))
    {
      ImGui::SetWindowFontScale(scale);
      DrawLock(IM_COL32(255, 200, 60, 255));
      ImGui::SameLine();
      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s  -  slot %d", s_pwReq.serverName.empty() ? s_pwReq.host.c_str() : s_pwReq.serverName.c_str(), s_pwReq.slot);
      ImGui::Spacing();
      if (s_pwWrong)
        PonUI::Text("WRONG PASSWORD. PLEASE TRY AGAIN.", PonUI::cError);
      else
        PonUI::Text("THIS HOST NEEDS A PASSWORD.", PonUI::cLabel);
      ImGui::Spacing();
      if (s_pwFocus)
      {
        ImGui::SetKeyboardFocusHere();
        s_pwFocus = false;
      }
      ImGui::SetNextItemWidth(320.0f * scale);
      bool enter = ImGui::InputText("##roompw", s_pwBuf, sizeof(s_pwBuf), ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::Spacing();
      bool join = PonUI::Button("JOIN", ImVec2(150.0f * scale, 40.0f * scale), PonUI::Kind::Primary) || enter;
      ImGui::SameLine(0, 10.0f * scale);
      bool cancel = PonUI::Button("CANCEL", ImVec2(150.0f * scale, 40.0f * scale), PonUI::Kind::Normal);
      if (join && s_pwBuf[0] != '\0' && !updateRequired)
      {
        s_request = s_pwReq;
        s_request.password = s_pwBuf;
        s_request.requested = true;
        memset(s_pwBuf, 0, sizeof(s_pwBuf));
        ImGui::CloseCurrentPopup();
        result = Action::Connect;
      }
      else if (cancel)
      {
        memset(s_pwBuf, 0, sizeof(s_pwBuf));
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
    ImGui::PopStyleVar();

    ImGui::End();
    return result;
  }
}
