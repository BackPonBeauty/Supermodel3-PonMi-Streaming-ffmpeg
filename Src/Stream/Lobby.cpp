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
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

  static void SetListStatus(const std::string &s)
  {
    std::lock_guard<std::mutex> lock(s_listLock);
    s_listStatus = s;
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

  // 11/19 (owner): the "Pad" row - the connected pads (SDL joysticks); the choice is saved at once
  static void DrawPadRow(float scale)
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
    std::string preview;
    if (cur >= 0)
      preview = std::to_string(cur + 1) + ": " + names[cur];
    else if (n == 0)
      preview = "No pad connected";
    else if (!s_padName.empty())
      preview = s_padName + " (not connected - pad 1 is used)";
    else
      preview = "Pad " + std::to_string(s_padIndex + 1) + " (not connected - pad 1 is used)";

    ImGui::Text("Pad");
    ImGui::SameLine(140.0f * scale);
    ImGui::SetNextItemWidth(420.0f * scale);
    if (ImGui::BeginCombo("##pad", preview.c_str()))
    {
      for (int i = 0; i < n; i++)
      {
        std::string label = std::to_string(i + 1) + ": " + names[i];
        ImGui::PushID(i);
        if (ImGui::Selectable(label.c_str(), i == cur))
        {
          s_padIndex = i;
          s_padName = names[i];
          Save();
        }
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("sent to the host while connected");
    ImGui::Spacing();
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

  Action Draw(float scale)
  {
    Load();
    Action result = Action::None;

    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##Lobby", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

    ImGui::SetWindowFontScale(scale * 1.4f);
    ImGui::TextColored(ImVec4(0.0f, 0.93f, 1.0f, 1.0f), "SPIKEOUT FINAL EDITION - STREAMING LOBBY");
    ImGui::SetWindowFontScale(scale);
    ImGui::Separator();
    ImGui::Spacing();

    if (!s_message.empty())
    {
      ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s", s_message.c_str());
      ImGui::Spacing();
    }

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
    if (updateRequired)
    {
      ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f), "This Supermodel is too old (build %lld, newest %lld). Please update to connect.",
                         StreamVersion::kBuild, newestBuild);
      ImGui::SameLine();
      if (ImGui::SmallButton("DOWNLOAD PAGE"))
        ShellExecuteA(nullptr, "open", updateUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      ImGui::Spacing();
    }
    else
      ImGui::TextDisabled("build %lld", StreamVersion::kBuild);

    // --- Discord login (P6b, CL-07): needed to join hosts over the internet ---
    {
      DiscordAuth::State st = DiscordAuth::GetState();
      ImGui::Text("Discord");
      ImGui::SameLine(140.0f * scale);
      ImVec4 col = st == DiscordAuth::State::LoggedIn ? ImVec4(0.45f, 0.85f, 0.45f, 1.0f)
                 : st == DiscordAuth::State::Failed   ? ImVec4(1.0f, 0.5f, 0.4f, 1.0f)
                                                      : ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
      ImGui::TextColored(col, "%s", DiscordAuth::StateText().c_str());
      if (st == DiscordAuth::State::LoggedOut || st == DiscordAuth::State::Failed)
      {
        ImGui::SameLine();
        if (ImGui::SmallButton("LOGIN"))
          DiscordAuth::StartLogin();
      }
      else if (st == DiscordAuth::State::Waiting)
      {
        ImGui::SameLine();
        if (ImGui::SmallButton("CANCEL"))
          DiscordAuth::CancelLogin();
      }
      else if (st == DiscordAuth::State::LoggedIn)
      {
        ImGui::SameLine();
        if (ImGui::SmallButton("LOGOUT"))
          DiscordAuth::Logout();
      }
      ImGui::Spacing();
    }

    DrawPadRow(scale);

    // --- Host list (P6, LB-01..03): click a free slot to connect ---
    ImGui::Text("Hosts");
    if (!StreamSecrets::HasFirebase())
    {
      ImGui::TextDisabled("Host list is off: this build has no Firebase keys. Direct connection still works.");
    }
    else
    {
      std::vector<LobbyHostCpp> hosts;
      std::string status;
      {
        std::lock_guard<std::mutex> lock(s_listLock);
        hosts = s_hosts;
        status = s_listStatus;
      }
      ImGui::SameLine();
      ImGui::TextDisabled("%s", status.c_str());
      ImGui::SameLine();
      if (ImGui::SmallButton("REFRESH"))
        s_refresh = true;

      float listH = 180.0f * scale;
      if (ImGui::BeginTable("##hosts", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                            ImVec2(0, listH)))
      {
        ImGui::TableSetupColumn("Server", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Game", ImGuiTableColumnFlags_WidthFixed, 110.0f * scale);
        ImGui::TableSetupColumn("Slots (click a free one)", ImGuiTableColumnFlags_WidthFixed, 8 * 34.0f * scale);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < hosts.size(); i++)
        {
          const LobbyHostCpp &h = hosts[i];
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          const char *shownName = h.servername.empty() ? h.ip.c_str() : h.servername.c_str();
          if (h.locked)
          {
            DrawLock(IM_COL32(255, 200, 60, 255));
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Password required");
            ImGui::SameLine();
          }
          ImGui::Text("%s", shownName);
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
          ImGui::Text("%s", h.gametitle.c_str());
          ImGui::TableSetColumnIndex(2);
          for (int s = 1; s <= 8; s++)
          {
            if (s > 1)
              ImGui::SameLine();
            ImGui::PushID((int) (i * 16 + s));
            char label[8];
            snprintf(label, sizeof(label), "%d", s);
            const LobbyHostCpp::Slot &sl = h.slots[s];
            if (!sl.present)
            {
              ImGui::BeginDisabled();
              ImGui::Button("-", ImVec2(28.0f * scale, 0));
              ImGui::EndDisabled();
            }
            else if (sl.available)
            {
              ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.55f, 0.25f, 1.0f));
              ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.75f, 0.35f, 1.0f));
              ImGui::BeginDisabled(updateRequired);
              bool clicked = ImGui::Button(label, ImVec2(28.0f * scale, 0));
              ImGui::EndDisabled();
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
                if (h.locked)
                  AskPassword(req, false);   // SV-18: the password box first
                else
                {
                  s_request = req;
                  s_request.requested = true;
                  result = Action::Connect;
                }
              }
              ImGui::PopStyleColor(2);
              if (ImGui::IsItemHovered())
                ImGui::SetTooltip(h.locked ? "Slot %d: free - click to join (password required)" : "Slot %d: free - click to join", s);
            }
            else
            {
              ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.15f, 0.15f, 1.0f));
              ImGui::BeginDisabled();
              ImGui::Button(label, ImVec2(28.0f * scale, 0));
              ImGui::EndDisabled();
              ImGui::PopStyleColor();
              if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Slot %d: in use%s%s", s, sl.player.empty() ? "" : " by ", sl.player.c_str());
            }
            ImGui::PopID();
          }
        }
        ImGui::EndTable();
      }
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Direct connection (LB-04; login-less, this PC / home network only) ---
    ImGui::Text("Connect directly");
    ImGui::Spacing();
    float labelW = 140.0f * scale;
    ImGui::Text("Host IP");
    ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(260.0f * scale);
    ImGui::InputText("##host", s_hostBuf, sizeof(s_hostBuf));

    ImGui::Text("Slot");
    ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(260.0f * scale);
    const char *slots[] = { "1", "2", "3", "4", "5", "6", "7", "8" };
    int idx = s_slot - 1;
    if (ImGui::Combo("##slot", &idx, slots, 8))
      s_slot = idx + 1;
    ImGui::SameLine();
    int base = StreamProto::SlotBasePort(s_slot);
    ImGui::TextDisabled("ports %d-%d", base, base + 3);

    ImGui::Text("Your name");
    ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(260.0f * scale);
    ImGui::InputText("##name", s_name, sizeof(s_name));

    ImGui::Spacing();
    ImGui::Spacing();
    float btnW = 220.0f * scale, btnH = 44.0f * scale;
    ImGui::BeginDisabled(updateRequired);
    bool connectClicked = ImGui::Button("CONNECT", ImVec2(btnW, btnH));
    ImGui::EndDisabled();
    if (connectClicked && !updateRequired && s_hostBuf[0] != '\0')
    {
      s_request = StreamConnectRequest();
      s_request.requested = true;
      s_request.host = s_hostBuf;
      s_request.slot = s_slot;
      s_request.name = RequestName();
      s_request.serverName = s_hostBuf;
      Save();
      result = Action::Connect;   // a host with a password answers "password required" -> the box opens
    }
    ImGui::SameLine();
    if (ImGui::Button("SKIP (PLAY LOCALLY)", ImVec2(btnW, btnH)))
    {
      Save();
      result = Action::Skip;
    }
    ImGui::SameLine();
    if (ImGui::Button("BACK", ImVec2(120.0f * scale, btnH)))
    {
      Save();
      result = Action::Back;
    }

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::TextDisabled("While connected:  F11 fullscreen,  F3 latency,  Up/Down bitrate,  ESC leave (back here)");

    // --- SV-18: password box ---
    if (s_pwOpenRequest)
    {
      s_pwOpenRequest = false;
      s_pwFocus = true;
      ImGui::OpenPopup("Password##room");
    }
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Password##room", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
      DrawLock(IM_COL32(255, 200, 60, 255));
      ImGui::SameLine();
      ImGui::Text("%s  -  slot %d", s_pwReq.serverName.empty() ? s_pwReq.host.c_str() : s_pwReq.serverName.c_str(), s_pwReq.slot);
      ImGui::Spacing();
      if (s_pwWrong)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Wrong password. Please try again.");
      else
        ImGui::Text("This host needs a password.");
      ImGui::Spacing();
      if (s_pwFocus)
      {
        ImGui::SetKeyboardFocusHere();
        s_pwFocus = false;
      }
      ImGui::SetNextItemWidth(300.0f * scale);
      bool enter = ImGui::InputText("##roompw", s_pwBuf, sizeof(s_pwBuf), ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::Spacing();
      bool join = ImGui::Button("JOIN", ImVec2(140.0f * scale, 0)) || enter;
      ImGui::SameLine();
      bool cancel = ImGui::Button("CANCEL", ImVec2(140.0f * scale, 0));
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

    ImGui::End();
    return result;
  }
}
