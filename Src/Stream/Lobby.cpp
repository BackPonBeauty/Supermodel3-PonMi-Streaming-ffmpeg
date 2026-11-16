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

  static const char *kSettingsFile = "streamclient.ini";

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

    // --- Host list (P6, LB-01..03): click a free slot to connect ---
    ImGui::Text("Hosts");
    if (!StreamSecrets::HasFirebase())
    {
      ImGui::TextDisabled("Host list is off: Config/StreamingKeys.ini (Firebase keys) not found. Direct connection still works.");
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
          ImGui::Text("%s", h.servername.empty() ? h.ip.c_str() : h.servername.c_str());
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
                s_request.requested = true;
                s_request.host = addr;
                s_request.slot = s;
                s_request.name = RequestName();
                Save();
                result = Action::Connect;
              }
              ImGui::PopStyleColor(2);
              if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Slot %d: free - click to join", s);
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
      s_request.requested = true;
      s_request.host = s_hostBuf;
      s_request.slot = s_slot;
      s_request.name = RequestName();
      Save();
      result = Action::Connect;
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

    ImGui::End();
    return result;
  }
}
