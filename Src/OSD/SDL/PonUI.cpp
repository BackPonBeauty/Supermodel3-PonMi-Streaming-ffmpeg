/*
 * PonUI.cpp
 *
 * 11/24 (owner): PonNvram's look for the settings GUI and the lobby. See PonUI.h.
 */

#include "PonUI.h"
#include <GL/glew.h>
#include "Pkgs/imgui/imgui_internal.h"
#include <windows.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace PonUI
{
  // --- the game font ---------------------------------------------------------------------------------------------
  // 128 cells of 8 x 9 (the extra top row = the black top edge the tiles lack, as in PonNvram), 16 x 8 cells.
  // Pixels are grey (the glyph's brightness) so a tint multiplies them like PonNvram's Glyphs(tint).
  static const int kCellW = 8, kCellH = 9, kCols = 16, kRows = 8;
  static const int kTexW = kCellW * kCols, kTexH = kCellH * kRows;
  static std::vector<uint32_t> s_pixels;   // RGBA (ABGR in memory), kTexW * kTexH
  static bool s_have[128] = {};
  static bool s_tried = false, s_loaded = false;
  static GLuint s_tex = 0;

  static bool ReadTileGen(const fs::path &file, std::vector<uint8_t> &vram)
  {
    std::ifstream f(file, std::ios::binary);
    if (!f)
      return false;
    f.seekg(0, std::ios::end);
    const uint64_t len = (uint64_t) f.tellg();
    uint64_t pos = 0;
    while (pos + 12 <= len)
    {
      f.seekg((std::streamoff) pos);
      uint32_t hdr[3];
      if (!f.read((char *) hdr, 12))
        return false;
      const uint32_t size = hdr[0];
      const int32_t nlen = (int32_t) hdr[1], clen = (int32_t) hdr[2];
      if (size < 12 || nlen < 1 || nlen > 1025 || clen < 0 || clen > 1025)
        return false;
      std::string name((size_t) nlen, '\0');
      f.read(&name[0], nlen);
      if (std::strcmp(name.c_str(), "Tile Generator") == 0)
      {
        vram.resize(0x120000);
        f.seekg((std::streamoff) (pos + 12 + nlen + clen));
        return (bool) f.read((char *) vram.data(), (std::streamsize) vram.size());
      }
      pos += size;
    }
    return false;
  }

  static bool Build(const std::vector<uint8_t> &v)
  {
    auto word = [&v](size_t off) { uint32_t w; std::memcpy(&w, &v[off], 4); return w; };
    uint32_t pal[256];
    for (int i = 0; i < 256; i++)
    {
      const uint32_t w = word(0x100000 + (size_t) i * 4);
      const uint32_t a = ((w >> 15) & 1) ? 0 : 255;
      const uint32_t r = (w & 31) * 255 / 31, g = ((w >> 5) & 31) * 255 / 31, b = ((w >> 10) & 31) * 255 / 31;
      pal[i] = (a << 24) | (r << 16) | (g << 8) | b;   // ARGB
    }
    auto pix = [&](int ch, int x, int y) -> int {
      const uint32_t w = word(((size_t) ch * 16 + (size_t) y * 2 + (size_t) x / 4) * 4);
      return (int) ((w >> ((3 - x % 4) * 8)) & 0xFF);
    };
    auto count = [&](int ch) {
      int n = 0;
      for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
        {
          const int p = pix(ch, x, y);
          if (p != 0 && (pal[p] >> 24) != 0)
            n++;
        }
      return n;
    };
    // the glyphs must look like text ('A' and '0' have pixels, ' ' none), as PonNvram checks
    if (count('A') < 12 || count('0') < 12 || count(' ') != 0 || pal[1] == pal[2])
      return false;

    s_pixels.assign((size_t) kTexW * kTexH, 0);
    for (int ch = 0x21; ch <= 0x7E; ch++)
    {
      const int cx = (ch % kCols) * kCellW, cy = (ch / kCols) * kCellH;
      bool any = false;
      for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
        {
          const int p = pix(ch, x, y);
          if (p == 0 || (pal[p] >> 24) == 0)
            continue;
          any = true;
          const uint32_t c = pal[p];
          double l = (double) (((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255)) / 3.0 / 255.0;
          l = std::min(1.0, l * 1.12);
          const uint32_t g = (uint32_t) (l * 255.0 + 0.5);
          s_pixels[(size_t) (cy + y + 1) * kTexW + cx + x] = 0xFF000000u | (g << 16) | (g << 8) | g;
          if (y == 0)
            s_pixels[(size_t) cy * kTexW + cx + x] = 0xFF000000u;   // the black top edge
        }
      s_have[ch] = any;
    }
    return true;
  }

  static void LoadFont()
  {
    if (s_tried)
      return;
    s_tried = true;
    std::vector<fs::path> dirs;
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::error_code ec;
    fs::path exeDir = fs::path(exe).parent_path();
    dirs.push_back(fs::current_path(ec) / "Saves");
    dirs.push_back(exeDir / "Saves");
    for (const auto &d : fs::directory_iterator(exeDir.parent_path(), ec))   // the sibling node folders (PonMi\01 ...)
      if (d.is_directory(ec))
        dirs.push_back(d.path() / "Saves");
    std::vector<std::pair<fs::file_time_type, fs::path>> files;
    for (const auto &d : dirs)
      for (const auto &e : fs::directory_iterator(d, ec))
      {
        const std::string ext = e.path().extension().string();
        if (e.is_regular_file(ec) && ext.rfind(".st", 0) == 0)
          files.emplace_back(e.last_write_time(ec), e.path());
      }
    std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    int tries = 0;
    for (const auto &f : files)
    {
      if (++tries > 12)
        break;
      std::vector<uint8_t> vram;
      if (ReadTileGen(f.second, vram) && Build(vram))
      {
        s_loaded = true;
        return;
      }
    }
  }

  void Init()
  {
    LoadFont();
    ApplyStyle();
    if (s_loaded && !s_tex)
    {
      glGenTextures(1, &s_tex);
      glBindTexture(GL_TEXTURE_2D, s_tex);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kTexW, kTexH, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_pixels.data());
      glBindTexture(GL_TEXTURE_2D, 0);
    }
  }

  static ImFont *s_sysFont = nullptr;
  void SetSystemFont(ImFont *font) { s_sysFont = font; }
  ImFont *SystemFont() { return s_sysFont ? s_sysFont : ImGui::GetFont(); }

  ImFont *AddWindowsFont(float size, bool bold)
  {
    static const char *const regular[] = { "C:\\Windows\\Fonts\\YuGothM.ttc", "C:\\Windows\\Fonts\\meiryo.ttc", "C:\\Windows\\Fonts\\msgothic.ttc" };
    static const char *const heavy[] = { "C:\\Windows\\Fonts\\YuGothB.ttc", "C:\\Windows\\Fonts\\meiryob.ttc", "C:\\Windows\\Fonts\\meiryo.ttc", "C:\\Windows\\Fonts\\msgothic.ttc" };
    const char *const *list = bold ? heavy : regular;
    const int n = bold ? 4 : 3;
    for (int i = 0; i < n; i++)
    {
      std::error_code ec;
      if (!fs::exists(list[i], ec))
        continue;
      ImFontConfig cfg;
      cfg.PixelSnapH = true;
      if (ImFont *f = ImGui::GetIO().Fonts->AddFontFromFileTTF(list[i], size, &cfg))
        return f;
    }
    return nullptr;
  }

  void Shutdown()
  {
    if (s_tex)
      glDeleteTextures(1, &s_tex);
    s_tex = 0;
  }

  bool FontLoaded()
  {
    return s_loaded && s_tex != 0;
  }

  static ImVec4 V4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

  void ApplyStyle()
  {
    ImGuiStyle &s = ImGui::GetStyle();
    s.WindowRounding = 0.0f;
    s.ChildRounding = 8.0f;
    s.FrameRounding = 6.0f;
    s.PopupRounding = 6.0f;
    s.GrabRounding = 6.0f;
    s.ScrollbarRounding = 6.0f;
    s.TabRounding = 6.0f;
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 0.0f;
    s.FrameBorderSize = 0.0f;
    s.PopupBorderSize = 1.0f;
    ImVec4 *c = s.Colors;
    c[ImGuiCol_Text] = V4(cText);
    c[ImGuiCol_TextDisabled] = V4(cDim);
    c[ImGuiCol_WindowBg] = V4(cBack);
    c[ImGuiCol_ChildBg] = V4(cPanel);
    c[ImGuiCol_PopupBg] = V4(IM_COL32(20, 26, 40, 250));
    c[ImGuiCol_Border] = V4(cLine);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = V4(cPill);
    c[ImGuiCol_FrameBgHovered] = V4(cPillHot);
    c[ImGuiCol_FrameBgActive] = V4(IM_COL32(44, 56, 84, 255));
    c[ImGuiCol_TitleBg] = V4(cPanel);
    c[ImGuiCol_TitleBgActive] = V4(cPanel);
    c[ImGuiCol_TitleBgCollapsed] = V4(cPanel);
    c[ImGuiCol_MenuBarBg] = V4(cPanel);
    c[ImGuiCol_ScrollbarBg] = V4(cPanel);
    c[ImGuiCol_ScrollbarGrab] = V4(IM_COL32(50, 60, 86, 255));
    c[ImGuiCol_ScrollbarGrabHovered] = V4(IM_COL32(70, 84, 116, 255));
    c[ImGuiCol_ScrollbarGrabActive] = V4(IM_COL32(0, 200, 230, 255));
    c[ImGuiCol_CheckMark] = V4(cAccent);
    c[ImGuiCol_SliderGrab] = V4(IM_COL32(0, 200, 230, 255));
    c[ImGuiCol_SliderGrabActive] = V4(IM_COL32(40, 240, 255, 255));
    c[ImGuiCol_Button] = V4(cPill);
    c[ImGuiCol_ButtonHovered] = V4(cPillHot);
    c[ImGuiCol_ButtonActive] = V4(IM_COL32(44, 56, 84, 255));
    c[ImGuiCol_Header] = V4(IM_COL32(0, 120, 150, 200));
    c[ImGuiCol_HeaderHovered] = V4(cPillHot);
    c[ImGuiCol_HeaderActive] = V4(IM_COL32(0, 150, 180, 255));
    c[ImGuiCol_Separator] = V4(cLine);
    c[ImGuiCol_SeparatorHovered] = V4(cAccent);
    c[ImGuiCol_SeparatorActive] = V4(cAccent);
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = V4(cPillHot);
    c[ImGuiCol_ResizeGripActive] = V4(cAccent);
    c[ImGuiCol_Tab] = V4(cPanel);
    c[ImGuiCol_TabHovered] = V4(cPillHot);
    c[ImGuiCol_TabSelected] = V4(cPill);
    c[ImGuiCol_TableHeaderBg] = V4(cPill);
    c[ImGuiCol_TableBorderStrong] = V4(cLine);
    c[ImGuiCol_TableBorderLight] = V4(cLine);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
    c[ImGuiCol_TextSelectedBg] = V4(IM_COL32(0, 150, 180, 120));
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
    c[ImGuiCol_NavCursor] = V4(cAccent);
  }

  // --- drawing ---------------------------------------------------------------------------------------------------
  static bool IsPlain(const char *t)
  {
    for (; *t; t++)
      if ((unsigned char) *t >= 0x80)
        return false;
    return true;
  }

  int Scale()
  {
    return std::max(1, (int) (ImGui::GetFontSize() / 8.0f));
  }

  ImVec2 Measure(const char *text, int sc)
  {
    if (!FontLoaded() || !IsPlain(text))
    {
      ImFont *f = ImGui::GetFont();
      return f->CalcTextSizeA(8.0f * sc * 1.1f, FLT_MAX, 0.0f, text);
    }
    return ImVec2((float) (std::strlen(text) * 8 * sc), (float) (8 * sc));
  }

  void Draw(ImDrawList *dl, ImVec2 pos, const char *text, int sc, ImU32 col)
  {
    if (!FontLoaded() || !IsPlain(text))
    {
      dl->AddText(ImGui::GetFont(), 8.0f * sc * 1.1f, pos, col, text);
      return;
    }
    const ImTextureID tex = (ImTextureID) (intptr_t) s_tex;
    float x = (float) (int) pos.x, y = (float) (int) pos.y;
    for (const char *p = text; *p; p++, x += 8.0f * sc)
    {
      int ch = (unsigned char) *p;
      if (ch >= 'a' && ch <= 'z')
        ch -= 32;
      if (ch <= 0x20 || ch >= 0x7F || !s_have[ch])
        continue;
      const float u0 = (float) ((ch % kCols) * kCellW) / kTexW, v0 = (float) ((ch / kCols) * kCellH) / kTexH;
      const float u1 = u0 + (float) kCellW / kTexW, v1 = v0 + (float) kCellH / kTexH;
      dl->AddImage(tex, ImVec2(x, y - (float) sc), ImVec2(x + 8.0f * sc, y + 8.0f * sc), ImVec2(u0, v0), ImVec2(u1, v1), col);
    }
  }

  int Fit(const char *text, int sc, float width)
  {
    while (sc > 1 && Measure(text, sc).x > width)
      sc--;
    return std::max(1, sc);
  }

  static void DrawCentered(ImDrawList *dl, const ImRect &r, const char *text, int sc, ImU32 col)
  {
    ImVec2 sz = Measure(text, sc);
    Draw(dl, ImVec2(r.Min.x + (r.GetWidth() - sz.x) * 0.5f, r.Min.y + (r.GetHeight() - sz.y) * 0.5f), text, sc, col);
  }

  // --- widgets ---------------------------------------------------------------------------------------------------
  void Text(const char *text, ImU32 col, int sc)
  {
    if (sc <= 0)
      sc = Scale();
    const ImVec2 sz = Measure(text, sc);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = std::max(sz.y, ImGui::GetTextLineHeight());
    Draw(ImGui::GetWindowDrawList(), ImVec2(p.x, p.y + (h - sz.y) * 0.5f), text, sc, col);
    ImGui::Dummy(ImVec2(sz.x, h));
  }

  void TextF(ImU32 col, const char *fmt, ...)
  {
    char buf[1024];
    va_list vl;
    va_start(vl, fmt);
    vsnprintf(buf, sizeof(buf), fmt, vl);
    va_end(vl);
    Text(buf, col);
  }

  void Label(const char *text, float maxWidth, ImU32 col, int sc0)
  {
    const int sc = Fit(text, sc0 > 0 ? sc0 : Scale(), maxWidth);
    const ImVec2 sz = Measure(text, sc);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    Draw(ImGui::GetWindowDrawList(), ImVec2(p.x, p.y + (h - sz.y) * 0.5f), text, sc, col);
    ImGui::Dummy(ImVec2(sz.x, h));
  }

  void Title(const char *text, ImU32 col, int sc)
  {
    Text(text, col, sc);
  }

  void Separator()
  {
    ImGui::Dummy(ImVec2(0, 2.0f));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + w, p.y), cLine, std::max(1.0f, ImGui::GetFontSize() / 16.0f));
    ImGui::Dummy(ImVec2(0, 4.0f));
  }

  static void KindColours(Kind k, bool hot, bool enabled, ImU32 &fill, ImU32 &text)
  {
    if (!enabled)
    {
      fill = cPill;
      text = IM_COL32(70, 80, 100, 255);
      return;
    }
    text = IM_COL32(255, 255, 255, 255);
    switch (k)
    {
    case Kind::Primary: fill = hot ? IM_COL32(40, 240, 255, 255) : IM_COL32(0, 200, 230, 255); break;
    case Kind::Go:      fill = hot ? IM_COL32(96, 214, 147, 255) : IM_COL32(40, 190, 100, 255); break;
    case Kind::Stop:    fill = hot ? IM_COL32(231, 128, 128, 255) : IM_COL32(220, 70, 70, 255); break;
    case Kind::Netlink: fill = hot ? IM_COL32(60, 130, 230, 255) : IM_COL32(30, 95, 190, 255); break;
    default:            fill = hot ? cPillHot : cPill; text = cText; break;
    }
  }

  bool Button(const char *label, ImVec2 size, Kind kind, bool enabled)
  {
    std::string shown(label);
    const size_t hash = shown.find("##");
    if (hash != std::string::npos)
      shown.resize(hash);
    int sc = Scale();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (size.x <= 0.0f)
      size.x = size.x < 0.0f ? avail.x + size.x + (size.x == -1.0f ? 1.0f : 0.0f) : Measure(shown.c_str(), sc).x + 16.0f * sc / 2 + 16.0f;
    if (size.y <= 0.0f)
      size.y = size.y < 0.0f ? std::max(ImGui::GetFrameHeight(), avail.y) : ImGui::GetFrameHeight() + 8.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    const bool clicked = ImGui::InvisibleButton("##ponbtn", size) && enabled;
    const bool hot = enabled && ImGui::IsItemHovered();
    ImGui::PopID();
    ImU32 fill, text;
    KindColours(kind, hot, enabled, fill, text);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImRect r(p, ImVec2(p.x + size.x, p.y + size.y));
    dl->AddRectFilled(r.Min, r.Max, fill, std::min(10.0f, size.y * 0.25f));
    sc = Fit(shown.c_str(), sc + (size.y > 40.0f * sc / 2.0f ? 1 : 0), size.x - 12.0f);
    while (sc > 1 && 8.0f * sc > size.y - 6.0f)   // and to its height
      sc--;
    DrawCentered(dl, r, shown.c_str(), sc, text);
    return clicked;
  }

  bool SmallButton(const char *label, Kind kind, bool enabled)
  {
    std::string shown(label);
    const size_t hash = shown.find("##");
    if (hash != std::string::npos)
      shown.resize(hash);
    const int sc = std::max(1, Scale() - 1);
    const ImVec2 sz = Measure(shown.c_str(), sc);
    const ImVec2 size(sz.x + 18.0f, ImGui::GetFrameHeight());
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    const bool clicked = ImGui::InvisibleButton("##ponsbtn", size) && enabled;
    const bool hot = enabled && ImGui::IsItemHovered();
    ImGui::PopID();
    ImU32 fill, text;
    KindColours(kind, hot, enabled, fill, text);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImRect r(p, ImVec2(p.x + size.x, p.y + size.y));
    dl->AddRectFilled(r.Min, r.Max, fill, 5.0f);
    DrawCentered(dl, r, shown.c_str(), sc, text);
    return clicked;
  }

  // the pill: "<  value  >"; returns -1 (left half clicked), +1 (right half), 0
  static int Pill(const char *id, const char *value, float w, float h, ImU32 valueCol)
  {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("##pill", ImVec2(w, h));
    const bool hot = ImGui::IsItemHovered();
    ImGui::PopID();
    const float mx = ImGui::GetIO().MousePos.x;
    const bool leftHalf = mx < p.x + w * 0.5f;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImRect r(p, ImVec2(p.x + w, p.y + h));
    dl->AddRectFilled(r.Min, r.Max, hot ? cPillHot : cPill, std::min(8.0f, h * 0.3f));
    if (hot)   // the half under the mouse a little brighter
      dl->AddRectFilled(leftHalf ? r.Min : ImVec2(r.Min.x + w * 0.5f, r.Min.y), leftHalf ? ImVec2(r.Min.x + w * 0.5f, r.Max.y) : r.Max,
                        IM_COL32(255, 255, 255, 22), std::min(8.0f, h * 0.3f));
    const int sc = Scale();
    const int asc = std::max(1, sc - 1);
    const float aw = 8.0f * asc;
    const ImU32 arrowL = hot && leftHalf ? cAccent : IM_COL32(70, 82, 106, 255);
    const ImU32 arrowR = hot && !leftHalf ? cAccent : IM_COL32(70, 82, 106, 255);
    Draw(dl, ImVec2(r.Min.x + 8.0f, r.Min.y + (h - 8.0f * asc) * 0.5f), "<", asc, arrowL);
    Draw(dl, ImVec2(r.Max.x - 8.0f - aw, r.Min.y + (h - 8.0f * asc) * 0.5f), ">", asc, arrowR);
    const float inner = w - 2.0f * (aw + 14.0f);
    const int vsc = Fit(value, sc, inner);
    const ImRect vr(ImVec2(r.Min.x + aw + 14.0f, r.Min.y), ImVec2(r.Max.x - aw - 14.0f, r.Max.y));
    dl->PushClipRect(vr.Min, vr.Max, true);
    DrawCentered(dl, vr, value, vsc, valueCol);
    dl->PopClipRect();
    if (!clicked)
      return 0;
    return leftHalf ? -1 : 1;
  }

  static float RowHeight()
  {
    return std::max(ImGui::GetFrameHeight(), 11.0f * Scale());
  }

  static float DefaultPillW()
  {
    return std::max(120.0f, 13.0f * 8.0f * Scale() * 0.5f + 60.0f);
  }

  // one size for every row label in a panel: the largest at which a 16-letter label fits beside the pill
  int RowScale()
  {
    const float avail = ImGui::GetContentRegionAvail().x;
    const float pillW = std::min(DefaultPillW(), avail * 0.6f);
    int sc = Scale();
    while (sc > 1 && 16.0f * 8.0f * sc > avail - pillW - 12.0f)
      sc--;
    return sc;
  }

  // label at the left, the pill right-aligned
  static int Row(const char *label, const char *value, float pillW, ImU32 valueCol)
  {
    if (pillW <= 0.0f)
      pillW = DefaultPillW();
    const float h = RowHeight();
    const float avail = ImGui::GetContentRegionAvail().x;
    pillW = std::min(pillW, avail * 0.6f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    std::string shown(label);
    const size_t hash = shown.find("##");
    if (hash != std::string::npos)
      shown.resize(hash);
    if (!shown.empty())
    {
      const int sc = Fit(shown.c_str(), RowScale(), avail - pillW - 12.0f);
      const ImVec2 sz = Measure(shown.c_str(), sc);
      Draw(ImGui::GetWindowDrawList(), ImVec2(p.x, p.y + (h - sz.y) * 0.5f), shown.c_str(), sc, cLabel);
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x + avail - pillW, p.y));
    const int d = Pill(label, value, pillW, h, cText);
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::Dummy(ImVec2(avail, ImGui::GetStyle().ItemSpacing.y * 0.5f));
    return d;
  }

  bool Toggle(const char *label, bool *v, float pillW)
  {
    const int d = Row(label, *v ? "ON" : "OFF", pillW, cText);
    if (d != 0)
      *v = !*v;
    return d != 0;
  }

  bool Choice(const char *label, int *idx, const char *const *items, int count, float pillW)
  {
    if (count <= 0)
      return false;
    if (*idx < 0 || *idx >= count)
      *idx = 0;
    const int d = Row(label, items[*idx], pillW, cText);
    if (d != 0)
      *idx = (*idx + d + count) % count;
    return d != 0;
  }

  bool Choice(const char *label, int *idx, const std::vector<std::string> &items, float pillW)
  {
    std::vector<const char *> p;
    for (const auto &s : items)
      p.push_back(s.c_str());
    return Choice(label, idx, p.data(), (int) p.size(), pillW);
  }

  bool PillToggle(const char *id, bool *v, float w)
  {
    const int d = Pill(id, *v ? "ON" : "OFF", w, RowHeight(), cText);
    if (d != 0)
      *v = !*v;
    return d != 0;
  }

  bool PillChoice(const char *id, int *idx, const char *const *items, int count, float w)
  {
    if (count <= 0)
      return false;
    if (*idx < 0 || *idx >= count)
      *idx = 0;
    const int d = Pill(id, items[*idx], w, RowHeight(), cText);
    if (d != 0)
      *idx = (*idx + d + count) % count;
    return d != 0;
  }

  bool PillChoice(const char *id, int *idx, const std::vector<std::string> &items, float w)
  {
    std::vector<const char *> p;
    for (const auto &s : items)
      p.push_back(s.c_str());
    return PillChoice(id, idx, p.data(), (int) p.size(), w);
  }

  bool Tabs(const char *id, const char *const *names, int count, int *cur)
  {
    const float avail = ImGui::GetContentRegionAvail().x;
    int sc = Scale() + 1;
    auto total = [&](int s) { float t = 0; for (int i = 0; i < count; i++) t += Measure(names[i], s).x + 12.0f * s; return t; };
    while (sc > 1 && total(sc) > avail)
      sc--;
    const float gap = std::max(12.0f * sc, (avail - total(sc) + 12.0f * sc * count) / count);
    const float h = 8.0f * sc + 14.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    bool changed = false;
    float x = p.x;
    ImGui::PushID(id);
    for (int i = 0; i < count; i++)
    {
      const float w = Measure(names[i], sc).x + gap;
      ImGui::SetCursorScreenPos(ImVec2(x, p.y));
      ImGui::PushID(i);
      if (ImGui::InvisibleButton("##tab", ImVec2(w, h)) && *cur != i)
      {
        *cur = i;
        changed = true;
      }
      const bool hot = ImGui::IsItemHovered();
      ImGui::PopID();
      const bool on = (*cur == i);
      const ImRect r(ImVec2(x, p.y), ImVec2(x + w, p.y + h - 4.0f));
      DrawCentered(dl, r, names[i], sc, on ? cTabOn : hot ? IM_COL32(255, 255, 255, 255) : cLabel);
      if (on)
      {
        const float tw = Measure(names[i], sc).x + 8.0f;
        dl->AddRectFilled(ImVec2(x + (w - tw) * 0.5f, p.y + h - 4.0f), ImVec2(x + (w + tw) * 0.5f, p.y + h - 4.0f + std::max(2.0f, (float) sc)), cTabOn);
      }
      x += w;
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 2.0f));
    dl->AddLine(ImVec2(p.x, p.y + h + 1.0f), ImVec2(p.x + avail, p.y + h + 1.0f), cLine, 1.0f);
    ImGui::Dummy(ImVec2(avail, 6.0f));
    return changed;
  }
}
