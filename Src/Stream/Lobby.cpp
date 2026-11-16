/*
 * Lobby.cpp
 *
 * Streaming (P5, 2026-11-16): lobby screen. See Lobby.h.
 */

#include "Lobby.h"
#include "Protocol.h"
#include "Pkgs/imgui/imgui.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace Lobby
{
  static bool s_open = false;
  static bool s_loaded = false;
  static std::string s_message;
  static StreamConnectRequest s_request;

  static char s_hostBuf[128] = "127.0.0.1";
  static char s_name[StreamProto::kMaxNameLength + 1] = "guest";
  static int  s_slot = 1;

  static const char *kSettingsFile = "streamclient.ini";

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

    // --- Host list (P6) ---
    ImGui::TextDisabled("Host list (Firebase) and Discord login: coming in the next step (P6).");
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
    if (ImGui::Button("CONNECT", ImVec2(btnW, btnH)) && s_hostBuf[0] != '\0')
    {
      s_request.requested = true;
      s_request.host = s_hostBuf;
      s_request.slot = s_slot;
      s_request.name = s_name[0] ? s_name : "guest";
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
