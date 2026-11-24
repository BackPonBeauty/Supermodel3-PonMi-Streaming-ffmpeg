/*
 * PonUI.h
 *
 * 11/24 (owner): the settings GUI and the streaming lobby in PonNvram's look (Launcher/PonNvram/MainForm.vb):
 * its colours, pills with < value >, orange tabs, green / red / cyan buttons, and the game's own 8x8 font.
 *
 * The font is not in the exe: like PonNvram (GameFont.vb) it is taken at start-up from a Supermodel save state
 * (Saves\*.st* next to the exe or in the sibling node folders: block "Tile Generator", tile n = ASCII n, palette at
 * VRAM 0x100000). Without a usable save state, the GUI's own font is used.
 */

#ifndef INCLUDED_PONUI_H
#define INCLUDED_PONUI_H

#include "Pkgs/imgui/imgui.h"
#include <string>
#include <vector>

namespace PonUI
{
  // PonNvram's colours
  constexpr ImU32 cBack    = IM_COL32(24, 29, 43, 255);
  constexpr ImU32 cPanel   = IM_COL32(18, 24, 38, 255);
  constexpr ImU32 cPill    = IM_COL32(30, 37, 56, 255);
  constexpr ImU32 cPillHot = IM_COL32(36, 46, 70, 255);
  constexpr ImU32 cLine    = IM_COL32(34, 42, 62, 255);
  constexpr ImU32 cText    = IM_COL32(235, 238, 245, 255);
  constexpr ImU32 cLabel   = IM_COL32(200, 205, 215, 255);
  constexpr ImU32 cDim     = IM_COL32(120, 132, 156, 255);
  constexpr ImU32 cFaint   = IM_COL32(90, 100, 120, 255);
  constexpr ImU32 cAccent  = IM_COL32(0, 230, 255, 255);
  constexpr ImU32 cChanged = IM_COL32(255, 196, 64, 255);
  constexpr ImU32 cWarn    = IM_COL32(255, 170, 60, 255);
  constexpr ImU32 cError   = IM_COL32(255, 92, 92, 255);
  constexpr ImU32 cOk      = IM_COL32(90, 230, 140, 255);
  constexpr ImU32 cTabOn   = IM_COL32(255, 140, 30, 255);

  // Load the font (once per process) and make its texture (per GL context: call after the context is made,
  // and Shutdown() before it goes). Also sets ImGui's colours.
  void Init();
  // 11/24: a Windows font (Gui.cpp adds it) for names; nullptr = the GUI font
  void SetSystemFont(ImFont *font);
  ImFont *SystemFont();
  // 11/24 (owner): every text that is not in the game's font uses a Windows font. Adds Yu Gothic (Medium, or Bold
  // with bold; else Meiryo / MS Gothic) to the current ImGui context's atlas; nullptr when none of the files is there.
  ImFont *AddWindowsFont(float size, bool bold = false);
  void Shutdown();
  void ApplyStyle();
  bool FontLoaded();

  // Glyph scale for ordinary text: the GUI font's height / 8, at least 1
  int  Scale();
  // The game font (A-Z 0-9 and signs; letters are shown in capitals). Text with other characters (Japanese
  // names ...) is drawn with the GUI font instead.
  ImVec2 Measure(const char *text, int sc);
  void Draw(ImDrawList *dl, ImVec2 pos, const char *text, int sc, ImU32 col);
  // the largest scale <= sc at which text fits in width (1 at least)
  int  Fit(const char *text, int sc, float width);

  // --- widgets (they take the ImGui layout like the ImGui ones) ---
  void Text(const char *text, ImU32 col = cLabel, int sc = 0);                 // sc 0 = Scale()
  void TextF(ImU32 col, const char *fmt, ...);
  void Label(const char *text, float maxWidth, ImU32 col = cLabel, int sc = 0);   // shrinks to fit maxWidth
  int  RowScale();   // the label size of Toggle / Choice rows in the current width
  void Title(const char *text, ImU32 col, int sc);
  void Separator();

  enum class Kind { Normal, Primary, Go, Stop, Netlink };
  bool Button(const char *label, ImVec2 size, Kind kind = Kind::Normal, bool enabled = true);
  bool SmallButton(const char *label, Kind kind = Kind::Normal, bool enabled = true);

  // A row: the label on the left, a pill "<  value  >" on the right (its left half = previous, right half = next)
  bool Toggle(const char *label, bool *v, float pillW = 0.0f);
  bool Choice(const char *label, int *idx, const char *const *items, int count, float pillW = 0.0f);
  bool Choice(const char *label, int *idx, const std::vector<std::string> &items, float pillW = 0.0f);

  // The pill alone, at the cursor (for a label drawn before it)
  bool PillToggle(const char *id, bool *v, float w);
  bool PillChoice(const char *id, int *idx, const char *const *items, int count, float w);
  bool PillChoice(const char *id, int *idx, const std::vector<std::string> &items, float w);

  // The tab row (game font, the chosen one orange and underlined). Returns true when the tab changed.
  bool Tabs(const char *id, const char *const *names, int count, int *cur);
}

#endif
