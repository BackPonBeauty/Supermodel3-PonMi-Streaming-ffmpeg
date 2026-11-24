#include "SDLIncludes.h"
#include <GL/glew.h>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <thread>
#include <algorithm>
#include "GameLoader.h"
#include "../Pkgs/imgui/imgui.h"
#include "../Pkgs/imgui/imgui_impl_sdl2.h"
#include "../Pkgs/imgui/imgui_impl_opengl3.h"
#include "../Pkgs/imgui/imgui_internal.h"
#include "../Src/Util/NewConfig.h"
#include "Util/ConfigBuilders.h"
#include "../Src/OSD/SDL/SDLInputSystem.h"
#include "../Src/Inputs/Inputs.h"
#include "Main.h"
#include "Stream/Lobby.h"
#include "Stream/Version.h"
#include "Font01.h"
#include "PonUI.h"
#include <ctime>   // for time, localtime, strftime
#include <fstream> // for ofstream
#include <SDL.h>
#include <windows.h>
#include <shellapi.h>

// Add image loading library
#include "../Src/Graphics/stb_image.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN // Constant to prevent conflicts eeeeeekkkkk
#endif
#include <windows.h>
#include <shlobj.h>  // Required to prevent errors with BROWSEINFO
#include <objbase.h> // for CoTaskMemFree
#else
#include <stdio.h> // for Linux popen
#endif

#ifdef _WIN32
#include "../Src/OSD/Windows/DirectInputSystem.h"
#endif

static std::vector<std::string> resolutions;
static int selectedResIndex = 0;
static bool resLoaded = false;
static bool showPreviewWindow = false;
static int previewW = 0, previewH = 0;
static SDL_Window *g_PreviewWindow = nullptr; // New window for preview
static int previewPosX = 0;
static int previewPosY = 0;
static char bufPosX[16] = "0";
static char bufPosY[16] = "0";
static char bufPortIn[64] = "";
static char bufPortOut[64] = "";
static bool s_launchFromLobby = false;   // 11/24: the launch came from the lobby (SKIP / a NETLINK slot): Spikeout's ROM
static char bufAddressOut[256] = "";
static bool scrollToSelected = true;

// Global variables for image management
static GLuint g_GameTexture = 0;
static std::string g_LoadedImageName = "";
static int g_ImgWidth = 0;
static int g_ImgHeight = 0;
static int XResolution = 0;
static int YResolution = 0;
static float RefreshRate = 60.0f;
static bool record = false;
static bool replay = false;
static std::string replayFilename = "";
static std::string s_Dir = "";
namespace fs = std::filesystem;
static float uImageAreaRatioH = 0.5f;
static float uImageAreaRatioW = 0.5f;

static void SaveSupermodelConfig(const std::string &path, std::map<std::string, std::string> &updates)
{
    std::ifstream ifs(path);
    std::vector<std::string> newLines;
    std::string line;
    std::map<std::string, bool> updatedFlags;

    // 1. Read the existing file line by line and update if a matching key is found
    if (ifs.is_open())
    {
        while (std::getline(ifs, line))
        {
            bool matched = false;
            for (auto const &[key, val] : updates)
            {
                // Find lines starting with "Key =" or "Key="
                if (line.compare(0, key.length(), key) == 0)
                {
                    // Verify that '=' or a space immediately follows the key
                    size_t nextCharPos = key.length();
                    while (nextCharPos < line.length() && line[nextCharPos] == ' ')
                        nextCharPos++;

                    if (nextCharPos < line.length() && line[nextCharPos] == '=')
                    {
                        newLines.push_back(key + " = " + val);
                        updatedFlags[key] = true;
                        matched = true;
                        break;
                    }
                }
            }
            if (!matched)
                newLines.push_back(line); // Keep unmatched lines (comments, etc.) as they are
        }
        ifs.close();
    }

    // 2. Append any new items not present in the original file to the end
    for (auto const &[key, val] : updates)
    {
        if (!updatedFlags[key])
        {
            newLines.push_back(key + " = " + val);
        }
    }

    // 3. Overwrite and save to file
    std::ofstream ofs(path, std::ios::trunc);
    if (ofs.is_open())
    {
        for (const auto &l : newLines)
        {
            ofs << l << "\n";
        }
        ofs.close();
    }
}

// Texture loading function
static bool LoadTextureFromFile(const char *filename, GLuint *out_texture)
{
    int width, height, channels;
    unsigned char *data = stbi_load(filename, &width, &height, &channels, 4);
    if (data == NULL)
        return false;

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    *out_texture = tex;
    return true;
}

static bool LoadTextureFromFile(const char *filename, GLuint *out_texture, int *out_width, int *out_height)
{
    int width, height, channels;
    unsigned char *data = stbi_load(filename, &width, &height, &channels, 4);
    if (data == NULL)
        return false;

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);

    *out_texture = tex;
    *out_width = width;   // Save width
    *out_height = height; // Save height
    return true;
}

void ClosePreviewWindow()
{
    if (g_PreviewWindow)
    {
        // Get coordinates at the moment of closing
        SDL_GetWindowPosition(g_PreviewWindow, &previewPosX, &previewPosY);

        // Convert to string for textbox display
        snprintf(bufPosX, sizeof(bufPosX), "%d", previewPosX);
        snprintf(bufPosY, sizeof(bufPosY), "%d", previewPosY);

        SDL_DestroyWindow(g_PreviewWindow);
        g_PreviewWindow = nullptr;
    }
}

// --- Startup Logic ---
// Streaming (P5): name of the selected game (the lobby opens only for spikeofe)
static std::string GetSelectedGameName(int selectedGame, const std::map<std::string, Game> &games)
{
    int index = 0;
    for (auto &pair : games)
    {
        if (index == selectedGame)
            return pair.second.name;
        index++;
    }
    return "";
}

static std::string GetRomPath(int selectedGame, const std::map<std::string, Game> &games, Util::Config::Node &config)
{
    if (selectedGame >= 0)
    {
        int index = 0;
        std::string romDir = s_Dir;

        for (auto &pair : games)
        {
            if (selectedGame == index)
            {
                std::string fullPath = (std::filesystem::path(romDir) / (pair.second.name + ".zip")).string();
                printf("[ROM PATH]\n");
                printf("  Dir      : %s\n", romDir.c_str());
                printf("  GameName : %s\n", pair.second.name.c_str());
                printf("  FullPath : %s\n", fullPath.c_str());

                return fullPath;
            }
            index++;
        }
    }
    return "";
}

static bool CheckRomExists(
    int selectedGame,
    const std::map<std::string, Game> &games,
    Util::Config::Node &config)
{
    if (selectedGame < 0)
    {
        printf("[ROM CHECK] No game selected\n");
        return false;
    }

    std::string romDir;

    // Check if Dir exists (determined by exception since Has is missing)
    try
    {
        romDir = s_Dir;
    }
    catch (const std::exception &e)
    {
        printf("[ROM CHECK] Dir not found in config (%s)\n", e.what());
        return false;
    }

    std::replace(romDir.begin(), romDir.end(), '\\', '/');

    int index = 0;
    for (const auto &pair : games)
    {
        if (index == selectedGame)
        {

            std::string fullPath =
                (std::filesystem::path(romDir) /
                 (pair.second.name + ".zip"))
                    .string();

            printf("[ROM CHECK]\n");
            printf("  Dir      : %s\n", romDir.c_str());
            printf("  GameName : %s\n", pair.second.name.c_str());
            printf("  FullPath : %s\n", fullPath.c_str());

            if (std::filesystem::exists(fullPath))
            {
                printf("  EXISTS   : YES\n");
                return true;
            }
            else
            {
                printf("  EXISTS   : NO\n");
                return false;
            }
        }
        index++;
    }

    printf("[ROM CHECK] Game index not found\n");
    return false;
}

// --- GUI Layout (Note: function definitions correctly added) ---
// static void GUI(const ImGuiIO &io, Util::Config::Node &config, const std::map<std::string, Game> &games, int &selectedGameIndex, bool &exit, bool &exitLaunch, bool &saveSettings, SDL_Window *window , int &selectedResIndex)
void GUI(ImGuiIO &io, Util::Config::Node &config,
         const std::map<std::string, Game> &games, int &selectedGameIndex,
         bool &exit, bool &exitLaunch, bool &saveSettings, SDL_Window *window,
         int &selectedResIndex, int &engineSelection, bool &vVsync, bool &vQuadRendering,
         bool &vGPUMultiThreaded, bool &vMultiThreaded, bool &vMultiTexture,
         bool &vBorderless, bool &vTrueAR, bool &vOverlay, bool &vFullScreen, bool &vWideScreen,
         bool &vWideBackground, bool &vStretch, bool &vShowFrameRate, bool &vThrottle,
         bool &vNoWhiteFlash, bool &vHideCMD, bool &vDefaultScanline, bool &vTrueHz, int &superSampling, int &selectedCRT, int &selectedUpscale,
         int &ppcFreq, int &WindowXPosition, int &WindowYPosition, int &Scanline, int &Barrel,
         int &musicVol, int &sfxVol, int &balance, bool &vEmulateSound,
         bool &vEmulateDSB, bool &vFlipStereo, bool &vLegacySoundDSP,
         int &selectedInputType, int &selectedCrosshair, int &selectedStyle,
          bool &vForceFeedback, bool &vNetwork, bool &vSimulateNet, bool &vStreaming, std::string &vCodec)
{
    // Calculate base scale
    float scale = io.DisplaySize.y / 600.0f;
    if (scale < 0.5f)
        scale = 0.5f;

    if (!games.empty())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        {
            if (selectedGameIndex > 0)
            {
                selectedGameIndex--;
            }
            else
            {
                selectedGameIndex = (int)games.size() - 1; // Loop to the bottom
            }
            scrollToSelected = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        {
            if (selectedGameIndex < (int)games.size() - 1)
            {
                selectedGameIndex++;
            }
            else
            {
                selectedGameIndex = 0; // Loop to the top
            }
            scrollToSelected = true;
        }
    }

    // 11/24 (owner): PonNvram's look (PonUI) - the "Japan Blue" buttons are gone

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("LauncherCanvas", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    // Base scale of the entire window
    ImGui::SetWindowFontScale(scale);
    const int psc = PonUI::Scale();
    PonUI::Title("SUPERMODEL", PonUI::cText, psc + 1);
    ImGui::SameLine(0, 14.0f * scale);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f * (psc + 1) - 8.0f * psc - 1.0f);
    PonUI::Title("PONMI EDITION", PonUI::cAccent, psc);
    // 11/24 (owner): the build number, small, after the title (bottoms lined up), as PonLuncher's version
    {
      const int bsc = std::max(1, psc - 1);
      char bb[32];
      snprintf(bb, sizeof(bb), "BUILD %lld", StreamVersion::kBuild);
      ImGui::SameLine(0, 14.0f * scale);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f * psc - 8.0f * bsc);
      PonUI::Title(bb, IM_COL32(120, 132, 156, 255), bsc);
    }
    PonUI::Separator();
    // float headerBottomY = ImGui::GetCursorPosY();

    // Calculate height
    float footerHeight = 50.0f * scale;   // 11/24: room for PonUI's buttons

    // Automatically calculate left column width to 50% of window

    float totalLeftHeight = 720.0f * scale;
    float totalLeftwidth = 1280.0f * scale;

    float currentImageWidth = totalLeftwidth * uImageAreaRatioW;
    // float splitterWidth = 8.0f;
    float currentImageHeight = totalLeftHeight * uImageAreaRatioH;
    float upperContentHeight = ImGui::GetContentRegionAvail().y - currentImageHeight - footerHeight;
    // float listAreaHeight = upperContentHeight - currentImageHeight - ImGui::GetStyle().ItemSpacing.y;
    float optionsHeight = upperContentHeight - ImGui::GetStyle().ItemSpacing.y + currentImageHeight;
    ImGui::BeginGroup(); // Set on the left side from here
    {
        if (ImGui::BeginChild("ImageArea", ImVec2(currentImageWidth, currentImageHeight), true))
        {
            ImGui::SetWindowFontScale(scale); // Re-apply scale

            // Embed image display logic
            if (selectedGameIndex >= 0)
            {
                int idx = 0;
                for (auto const &pair : games)
                {
                    if (idx == selectedGameIndex)
                    {
                        if (g_LoadedImageName != pair.second.name)
                        {
                            if (g_GameTexture)
                            {
                                glDeleteTextures(1, &g_GameTexture);
                                g_GameTexture = 0;
                            }
                            std::string imgPath = "Snaps/" + pair.second.name + ".png";
                            if (!LoadTextureFromFile(imgPath.c_str(), &g_GameTexture, &g_ImgWidth, &g_ImgHeight))
                            {
                                imgPath = "Snaps/" + pair.second.name + ".jpg";
                                LoadTextureFromFile(imgPath.c_str(), &g_GameTexture, &g_ImgWidth, &g_ImgHeight);
                            }
                            g_LoadedImageName = pair.second.name;
                        }
                        break;
                    }
                    idx++;
                }
            }

            if (g_GameTexture && g_ImgWidth > 0 && g_ImgHeight > 0)
            {
                float availW = ImGui::GetContentRegionAvail().x;
                float availH = ImGui::GetContentRegionAvail().y;

                // --- Aspect ratio maintenance calculation ---
                float ratioW = availW / (float)g_ImgWidth;
                float ratioH = availH / (float)g_ImgHeight;
                float ratio = (ratioW < ratioH) ? ratioW : ratioH; // Use the smaller ratio

                float drawW = (float)g_ImgWidth * ratio;
                float drawH = (float)g_ImgHeight * ratio;

                // Offset calculation for centering
                float offsetX = (availW - drawW) * 0.5f;
                float offsetY = (availH - drawH) * 0.5f;
                ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + offsetX, ImGui::GetCursorPosY() + offsetY));

                ImGui::Image((void *)(intptr_t)g_GameTexture, ImVec2(drawW, drawH));
            }
            else
            {
                PonUI::Text("(NO IMAGE)", PonUI::cFaint);
            }
        }
        ImGui::EndChild();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(currentImageWidth, 0));
        ImGui::Button("##splitter", ImVec2(currentImageWidth, 8.0f * scale));

        // Change cursor on mouse hover
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

        // Drag processing
        if (ImGui::IsItemActive())
        {
            float deltaRatio = ImGui::GetIO().MouseDelta.y / totalLeftHeight;
            uImageAreaRatioH += deltaRatio;

            // Limit within minimum and maximum sizes (important!)
            if (uImageAreaRatioH < 0.25f)
                uImageAreaRatioH = 0.25f;
            if (uImageAreaRatioH > 0.75f)
                uImageAreaRatioH = 0.75f;
        }
        ImGui::PopStyleVar();

        // --- Left: Game List ---
        if (ImGui::BeginChild("GameList", ImVec2(currentImageWidth, ImGui::GetContentRegionAvail().y - footerHeight), true))
        {
            ImGui::SetWindowFontScale(scale);

            static ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;

            if (ImGui::BeginTable("GameTable", 2, tableFlags))
            {
                ImGui::TableSetupColumn("Game Title", ImGuiTableColumnFlags_WidthStretch, 0.7f);
                ImGui::TableSetupColumn("ROM", ImGuiTableColumnFlags_WidthStretch, 0.3f);
                ImGui::TableHeadersRow();

                int i = 0;
                for (auto const &pair : games)
                {
                    const bool isSelected = (selectedGameIndex == i);
                    const Game &game = pair.second;
                    ImGui::PushID(game.name.c_str());

                    ImGui::TableNextRow(ImGuiTableRowFlags_None, 16.0f * scale);

                    ImGui::TableSetColumnIndex(0);
                    ImGui::SetWindowFontScale(scale);

                    std::string displayName = game.title.empty() ? game.name : game.title;

                    displayName.erase(std::remove(displayName.begin(), displayName.end(), '\n'), displayName.end());
                    displayName.erase(std::remove(displayName.begin(), displayName.end(), '\r'), displayName.end());

                    if (ImGui::Selectable(displayName.c_str(), selectedGameIndex == i,
                                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                    {
                        selectedGameIndex = i;
                    }
                    if (isSelected && scrollToSelected)
                    {
                        ImGui::SetScrollHereY(0.5f); // 0.5f brings it to the middle of the screen (0.0f top, 1.0f bottom)
                        scrollToSelected = false;    // Lower flag once executed
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetWindowFontScale(scale);
                    // Use TextUnformatted to prevent line break
                    std::string romName = game.name;
                    romName.erase(std::remove(romName.begin(), romName.end(), '\n'), romName.end());
                    romName.erase(std::remove(romName.begin(), romName.end(), '\r'), romName.end());

                    ImGui::TextUnformatted(game.name.c_str());

                    ImGui::PopID();
                    i++;
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndGroup();

    // 2. Left-Right Splitter
    ImGui::SameLine(0, 0); // Align horizontally with zero gap

    ImGui::InvisibleButton("##h_splitter", ImVec2(8.0f * scale, optionsHeight));
    // ImGui::SetWindowFontScale(scale);
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); // Left-Right arrows

    if (ImGui::IsItemActive())
    {
        float deltaRatio = ImGui::GetIO().MouseDelta.x / totalLeftwidth;
        uImageAreaRatioW += deltaRatio;

        // Limit within minimum and maximum sizes (important!)
        if (uImageAreaRatioW < 0.25f)
            uImageAreaRatioW = 0.25f;
        if (uImageAreaRatioW > 0.75f)
            uImageAreaRatioW = 0.75f;
    }

    // 3. Start of the Right Column
    ImGui::SameLine();

    ImGui::BeginGroup();
    {
        // Bottom-Right: Options
        if (ImGui::BeginChild("RightOptions", ImVec2(0, optionsHeight), true))
        {
            ImGui::SetWindowFontScale(scale); // Re-apply scale
            static int s_ponTab = 0;
            static const char *const kTabs[] = { "VIDEO", "SOUND", "CONTROL", "NETWORK", "OTHER", "ABOUT" };
            PonUI::Tabs("##settabs", kTabs, 6, &s_ponTab);
            const float labelW0 = 170.0f * scale;
            {
                if (s_ponTab == 0)
                {

                    // Lower part of the main GUI's right column
                    ImGui::Spacing();
                    PonUI::Separator();
                    // ImGui::Text("Last Preview Position:");

                    // --- new3D / Legacy ---
                    {
                        static const char *const engines[] = { "NEW3D", "LEGACY" };
                        PonUI::Choice("GRAPHICS ENGINE", &engineSelection, engines, 2);
                    }

                    ImGui::Spacing();
                    PonUI::Separator();
                    ImGui::Spacing();

                    // --- 2-column checkbox layout ---

                    // --- Column 1 ---
                    {
                        if (PonUI::Toggle("VSYNC", &vVsync))
                        {
                            saveSettings = true;
                        }
                        if (PonUI::Toggle("QUADRENDERING", &vQuadRendering))
                        {
                            saveSettings = true;
                        }
                        if (PonUI::Toggle("GPUMULTITHREADED", &vGPUMultiThreaded))
                        {
                            saveSettings = true;
                        }
                        if (PonUI::Toggle("MULTITHREADED", &vMultiThreaded))
                        {
                            saveSettings = true;
                        }
                        if (PonUI::Toggle("MULTITEXTURE", &vMultiTexture))
                        {
                            saveSettings = true;
                        }
                        if (PonUI::Toggle("BORDERLESS", &vBorderless))
                        {
                            saveSettings = true;
                        }
                        if (PonUI::Toggle("TRUE-HZ", &vTrueHz))
                        {
                            saveSettings = true;
                        }
                    }


                    // --- Column 2 ---
                    {
                        if (PonUI::Toggle("FULLSCREEN", &vFullScreen))
                        {
                            saveSettings = true;
                        }
                        if (PonUI::Toggle("WIDESCREEN", &vWideScreen))
                        {
                            saveSettings = true;
                        }
                        if (PonUI::Toggle("WIDEBACKGROUND", &vWideBackground))
                        {
                            saveSettings = true;
                        }
                        if (PonUI::Toggle("STRETCH", &vStretch))
                        {
                            saveSettings = true;
                        }
                        if (PonUI::Toggle("SHOWFRAMERATE", &vShowFrameRate))
                        {
                            saveSettings = true;
                        }
                        if (PonUI::Toggle("THROTTLE", &vThrottle))
                        {
                            saveSettings = true;
                        }
                        if (PonUI::Toggle("NOWHITEFLASH", &vNoWhiteFlash))
                        {
                            saveSettings = true;
                        }
                    }
                    {
                        if (PonUI::Toggle("TRUE-AR", &vTrueAR))
                        {
                            saveSettings = true;
                        }
                        if (PonUI::Toggle("OVERLAY", &vOverlay))
                        {
                            saveSettings = true;
                        }
                        if (PonUI::Toggle("HIDECMD", &vHideCMD))
                        {
                            saveSettings = true;
                        }
                        if (PonUI::Toggle("DEFAULTSCANLINE", &vDefaultScanline))
                        {
                            saveSettings = true;
                        }
                    }
                    ImGui::Spacing();
                    PonUI::Separator();
                    ImGui::Spacing();
                    int w, h;
                    static bool isFirstFrame = true;
                    if (isFirstFrame)
                    {
                        // 11/24: bufPosX / bufPosY are filled from the ini when the settings are read (empty stays empty)
                        (void) WindowXPosition; (void) WindowYPosition;
                        isFirstFrame = false;
                    }

                    if (PonUI::SmallButton("POSITION##Button"))
                    {

                        if (g_PreviewWindow)
                        {
                            SDL_DestroyWindow(g_PreviewWindow);
                            g_PreviewWindow = nullptr;
                        }

                        if (sscanf(resolutions[selectedResIndex].c_str(), "%dx%d", &w, &h) == 2)
                        {

                            // 1. Get index of the display where the main window currently is
                            // window is a pointer to the main window defined in RunGUI
                            int displayIndex = SDL_GetWindowDisplayIndex(window);
                            if (displayIndex < 0)
                                displayIndex = 0; // Reference display 0 on error

                            // 2. Get the display bounds (coordinates and size)
                            SDL_Rect rect;
                            if (SDL_GetDisplayBounds(displayIndex, &rect) == 0)
                            {

                                // 3. Calculate center position within the display
                                // rect.x, rect.y are the top-left start coordinates of the monitor
                                int posX = rect.x + (rect.w - w) / 2;
                                int posY = rect.y + (rect.h - h) / 2;

                                // 4. Create new window using SDL
                                g_PreviewWindow = SDL_CreateWindow(
                                    "Resolution Preview",
                                    posX, posY, w, h,
                                    SDL_WINDOW_SHOWN);
                            }
                        }
                    }

                    float availableWidth = ImGui::GetContentRegionAvail().x - labelW0; // Remaining width after subtracting label
                    float inputWidth = (availableWidth * 0.5f);                                 // Split in half

                    ImGui::SameLine(labelW0);

                    // --- X Coordinate ---
                    ImGui::PushItemWidth(inputWidth);
                    if (ImGui::InputText("##PosX", bufPosX, sizeof(bufPosX)))
                    {
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();

                    ImGui::SameLine();

                    // --- Y Coordinate ---
                    ImGui::PushItemWidth(inputWidth);
                    if (ImGui::InputText("##PosY", bufPosY, sizeof(bufPosY)))
                    {
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();

                    if (!resolutions.empty())
                        PonUI::Choice("RESOLUTION", &selectedResIndex, resolutions);
                    if (false && ImGui::BeginCombo("##Resolution", resolutions[selectedResIndex].c_str()))
                    {
                        ImGui::SetWindowFontScale(scale);
                        for (int i = 0; i < (int)resolutions.size(); i++)
                        {
                            const bool isSelected = (selectedResIndex == i);
                            if (ImGui::Selectable(resolutions[i].c_str(), isSelected))
                            {
                                selectedResIndex = i;
                                /*

                                // --- Parse values to save to ini (e.g. 1920x1080 -> X=1920, Y=1080) ---
                                int w, h;
                                if (sscanf(resolutions[i].c_str(), "%dx%d", &w, &h) == 2)
                                {
                                    XResolution = w;
                                    YResolution = h;
                                    saveSettings = true; // Set save flag
                                }
                                    */
                            }

                            // Initial focus
                            if (isSelected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }

                    // --- SuperSampling Slider (1-8) ---
                    // static int superSampling = 1; // actually read from config
                    PonUI::Label("SUPER SAMPLING", labelW0 - 8.0f, PonUI::cLabel, PonUI::RowScale());
                    ImGui::SameLine(labelW0);
                    ImGui::PushItemWidth(-1); // Expand slider to right edge
                    if (ImGui::SliderInt("##SS", &superSampling, 1, 8))
                    {
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();

                    // --- CRTColor Dropdown ---
                    const char *crtItems[] = {
                        "0=none",
                        "1=ARI/D93 (recommended for all JP developed games)",
                        "2=PVM_20M2U/D93",
                        "3=BT601_525/D93",
                        "4=BT601_525/D65 (recommended for all US developed games)",
                        "5=BT601_625/D65 (recommended for all EUR/AUS developed games)"};

                    // Use selectedCRT passed as argument
                    if (PonUI::Choice("CRT COLOR", &selectedCRT, crtItems, IM_ARRAYSIZE(crtItems)))
                        saveSettings = true;
                    ImGui::PushItemWidth(-1);
                    if (false && ImGui::BeginCombo("##CRTColor", crtItems[selectedCRT]))
                    {
                        ImGui::SetWindowFontScale(scale);
                        for (int n = 0; n < IM_ARRAYSIZE(crtItems); n++)
                        {
                            // Check if loop count matches selectedCRT
                            bool is_selected = (selectedCRT == n);
                            if (ImGui::Selectable(crtItems[n], is_selected))
                            {
                                // Assign selected index on choice
                                selectedCRT = n;
                                saveSettings = true;
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();

                    // --- UpscaleMode Dropdown (1-3) ---
                    const char *upscaleItems[] = {"0=none/sharp pixels", "1=biquintic", "2=bilinear", "3=bicubic"};
                    // static int selectedUpscale = 0;
                    if (PonUI::Choice("UPSCALE MODE", &selectedUpscale, upscaleItems, IM_ARRAYSIZE(upscaleItems)))
                        saveSettings = true;
                    ImGui::PushItemWidth(-1);
                    if (false && ImGui::BeginCombo("##UpscaleMode", upscaleItems[selectedUpscale]))
                    {
                        ImGui::SetWindowFontScale(scale);
                        for (int n = 0; n < IM_ARRAYSIZE(upscaleItems); n++)
                        {
                            bool is_selected = (selectedUpscale == n);
                            if (ImGui::Selectable(upscaleItems[n], is_selected))
                            {
                                selectedUpscale = n;
                                saveSettings = true;
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();
                    // static int ppcFreq = 57; // Default value
                    PonUI::Label("PPC FREQUENCY", labelW0 - 8.0f, PonUI::cLabel, PonUI::RowScale());
                    ImGui::SameLine(labelW0);
                    ImGui::PushItemWidth(-1);
                    if (ImGui::SliderInt("##PPCFreq", &ppcFreq, 0, 200))
                    {
                        // config.Set("PowerPCFrequency", (int64_t)ppcFreq); // Save to config if needed
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();
                    // static int Scanline = 1;
                    PonUI::Label("SCANLINE STRENGTH", labelW0 - 8.0f, PonUI::cLabel, PonUI::RowScale());
                    ImGui::SameLine(labelW0);
                    ImGui::PushItemWidth(-1);
                    if (ImGui::SliderInt("##Scanline", &Scanline, 1, 100))
                    {
                        // config.Set("PowerPCFrequency", (int64_t)ppcFreq); // Save to config if needed
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();
                    // static int Barrel = 1;
                    PonUI::Label("BARREL STRENGTH", labelW0 - 8.0f, PonUI::cLabel, PonUI::RowScale());
                    ImGui::SameLine(labelW0);
                    ImGui::PushItemWidth(-1);
                    if (ImGui::SliderInt("##Barrel", &Barrel, 0, 100))
                    {
                        // config.Set("PowerPCFrequency", (int64_t)ppcFreq); // Save to config if needed
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();
                }
                // --- Audio Tab ---
                if (s_ponTab == 1)
                {
                    ImGui::SetWindowFontScale(scale);
                    PonUI::Text("AUDIO SETTINGS", PonUI::cAccent);
                    PonUI::Separator();
                    ImGui::Spacing();

                    // Slider items
                    // static int musicVol = 100;
                    // static int sfxVol = 100;
                    // static int balance = 0;

                    float labelWidth = 140.0f * scale; // Align label widths

                    // Music
                    PonUI::Label("MUSIC", labelWidth - 8.0f, PonUI::cLabel, PonUI::RowScale());
                    ImGui::SameLine(labelWidth);
                    ImGui::PushItemWidth(-1);
                    if (ImGui::SliderInt("##Music", &musicVol, 0, 100))
                    {
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();

                    // Balance
                    PonUI::Label("BALANCE", labelWidth - 8.0f, PonUI::cLabel, PonUI::RowScale());
                    ImGui::SameLine(labelWidth);
                    ImGui::PushItemWidth(-1);
                    if (ImGui::SliderInt("##Balance", &balance, -100, 100))
                    {
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();

                    // Sound
                    PonUI::Label("SOUND", labelWidth - 8.0f, PonUI::cLabel, PonUI::RowScale());
                    ImGui::SameLine(labelWidth);
                    ImGui::PushItemWidth(-1);
                    if (ImGui::SliderInt("##Sound", &sfxVol, 0, 100))
                    {
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();

                    ImGui::Spacing();
                    PonUI::Separator();
                    ImGui::Spacing();

                    // --- Checkbox items ---
                    // Link with EmulateSound config
                    // static bool vEmulateSound = true;
                    // static bool vEmulateDSB = true;
                    // static bool vFlipStereo = false;
                    // static bool vLegacySoundDSP = false;

                    if (PonUI::Toggle("EMULATESOUND", &vEmulateSound))
                    {
                        saveSettings = true;
                    }
                    if (PonUI::Toggle("EMULATEDSB", &vEmulateDSB))
                    {
                        saveSettings = true;
                    }
                    if (PonUI::Toggle("FLIPSTEREO", &vFlipStereo))
                    {
                        saveSettings = true;
                    }
                    if (PonUI::Toggle("LEGACYSOUNDDSP", &vLegacySoundDSP))
                    {
                        saveSettings = true;
                    }

                }

                // --- Control Tab ---
                if (s_ponTab == 2)
                {
                    ImGui::SetWindowFontScale(scale);
                    PonUI::Text("CONTROL SETTINGS", PonUI::cAccent);
                    PonUI::Separator();
                    ImGui::Spacing();
                    float labelWidth = 150.0f * scale; // Align label widths

                    // --- Input Type ---
                    const char *inputTypes[] = {"Xinput", "Dinput", "Rawinput","SDL","NONE (streaming server)"};
                    // static int selectedInputType = 0; // Link with config
                    if (PonUI::Choice("INPUT TYPE", &selectedInputType, inputTypes, IM_ARRAYSIZE(inputTypes)))
                        saveSettings = true;
                    ImGui::PushItemWidth(-1);
                    if (false && ImGui::BeginCombo("##InputType", inputTypes[selectedInputType]))
                    {
                        ImGui::SetWindowFontScale(scale);
                        for (int n = 0; n < IM_ARRAYSIZE(inputTypes); n++)
                        {
                            if (ImGui::Selectable(inputTypes[n], selectedInputType == n))
                                selectedInputType = n;
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();

                    // --- CrossHairs ---
                    const char *crosshairTypes[] = {"Disable", "Player1", "Player2", "2Players"};
                    // static int selectedCrosshair = 0;
                    if (PonUI::Choice("CROSSHAIRS", &selectedCrosshair, crosshairTypes, IM_ARRAYSIZE(crosshairTypes)))
                        saveSettings = true;
                    ImGui::PushItemWidth(-1);
                    if (false && ImGui::BeginCombo("##CrossHairs", crosshairTypes[selectedCrosshair]))
                    {
                        ImGui::SetWindowFontScale(scale);
                        for (int n = 0; n < IM_ARRAYSIZE(crosshairTypes); n++)
                        {
                            if (ImGui::Selectable(crosshairTypes[n], selectedCrosshair == n))
                                selectedCrosshair = n;
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();

                    // --- Style (Vector / bmp) ---
                    const char *styleTypes[] = {"vector", "bmp"};
                    // static int selectedStyle = 0;
                    if (PonUI::Choice("STYLE", &selectedStyle, styleTypes, IM_ARRAYSIZE(styleTypes)))
                        saveSettings = true;
                    ImGui::PushItemWidth(-1);
                    if (false && ImGui::BeginCombo("##Style", styleTypes[selectedStyle]))
                    {
                        ImGui::SetWindowFontScale(scale);
                        for (int n = 0; n < IM_ARRAYSIZE(styleTypes); n++)
                        {
                            if (ImGui::Selectable(styleTypes[n], selectedStyle == n))
                                selectedStyle = n;
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();

                    ImGui::Spacing();
                    PonUI::Separator();
                    ImGui::Spacing();

                    // --- ForceFeedback ---
                    // static bool vForceFeedback = false;
                    if (PonUI::Toggle("FORCE FEEDBACK", &vForceFeedback))
                    {
                        saveSettings = true;
                    }

                    ImGui::Spacing();
                    PonUI::Separator();
                    ImGui::Spacing();

                    // --- Config Button ---
                    if (PonUI::Button("CONFIG", ImVec2(-1, 40.0f * scale), PonUI::Kind::Primary))
                    {
                        ShellExecuteA(NULL, "open", "Supermodel.exe", "-config-inputs", NULL, SW_SHOWNORMAL);

                        exitLaunch = false;
                        exit = true;
                        saveSettings = true;
                    }

                }

                // --- Network Tab ---
                if (s_ponTab == 3)
                {
                    ImGui::SetWindowFontScale(scale);
                    PonUI::Text("NETWORK SETTING", PonUI::cAccent);
                    PonUI::Separator();
                    ImGui::Spacing();
                    float labelWidth = 150.0f * scale; // Align label widths

                    // --- Checkbox items ---
                    // static bool vNetwork = false;
                    // static bool vSimulateNet = false;

                    if (PonUI::Toggle("NETWORK", &vNetwork))
                    {
                        saveSettings = true;
                    }
                    if (PonUI::Toggle("SIMULATENET", &vSimulateNet))
                    {
                        saveSettings = true;
                    }
                    if (PonUI::Toggle("STREAMING", &vStreaming))
                    {
                        saveSettings = true;
                    }

                    ImGui::Spacing();
                    PonUI::Separator();
                    ImGui::Spacing();

                    // Codec selection combo
                    {
                        const char* codecs[] = { "H264", "H265" };
                        int selectedCodec = (vCodec == "H264") ? 0 : 1;
                        ImGui::PushItemWidth(100.0f * scale);
                        if (PonUI::Choice("CODEC", &selectedCodec, codecs, IM_ARRAYSIZE(codecs)))
                        {
                            vCodec = codecs[selectedCodec];
                            saveSettings = true;
                        }
                        ImGui::PopItemWidth();
                    }

                    ImGui::Spacing();
                    PonUI::Separator();
                    ImGui::Spacing();

                    // --- Text input items ---
                    /*
                    static char bufPortIn[8] = "1970";
                    static char bufPortOut[8] = "1971";
                    static char bufAddressOut[128] = "127.0.0.1";
                    */
                    // PortIn
                    PonUI::Label("PORTIN", labelWidth - 8.0f, PonUI::cLabel, PonUI::RowScale());
                    ImGui::SameLine(labelWidth);
                    ImGui::PushItemWidth(100.0f * scale); // Short width for port number
                    if (ImGui::InputText("##PortIn", bufPortIn, sizeof(bufPortIn), ImGuiInputTextFlags_CharsDecimal))
                    {
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();

                    // PortOut
                    PonUI::Label("PORTOUT", labelWidth - 8.0f, PonUI::cLabel, PonUI::RowScale());
                    ImGui::SameLine(labelWidth);
                    ImGui::PushItemWidth(100.0f * scale);
                    if (ImGui::InputText("##PortOut", bufPortOut, sizeof(bufPortOut), ImGuiInputTextFlags_CharsDecimal))
                    {
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();

                    // AddressOut
                    PonUI::Label("ADDRESSOUT", labelWidth - 8.0f, PonUI::cLabel, PonUI::RowScale());
                    ImGui::SameLine(labelWidth);
                    ImGui::PushItemWidth(-1); // Expand address input to right edge
                    if (ImGui::InputText("##AddressOut", bufAddressOut, sizeof(bufAddressOut)))
                    {
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();

                }

                // --- Other Tab ---
                if (s_ponTab == 4)
                {
                    ImGui::SetWindowFontScale(scale);

                    // --- Replay Control Section ---
                    PonUI::Text("REPLAY SYSTEM", PonUI::cAccent);
                    PonUI::Separator();
                    ImGui::Spacing();

                    // Group into Child Window for Replay
                    ImGui::BeginChild("ReplayControl", ImVec2(0, 150 * scale), true); // Slightly taller child window
                    {
                        ImGui::SetWindowFontScale(scale);

                        // 1. Scan for .rec files
                        static std::vector<std::string> replayFiles;
                        static int selectedFileIdx = -1;
                        static float lastScanTime = 0;

                        // Re-scan folder every 5 seconds
                        float currentTime = (float)ImGui::GetTime();
                        if (currentTime - lastScanTime > 5.0f || (replayFiles.empty() && lastScanTime == 0))
                        {
                            std::string folderPath = "Replays";

                            // 1. Create folder if missing
                            if (!fs::exists(folderPath))
                            {
                                fs::create_directory(folderPath);
                            }

                            // Save current selection name temporarily
                            std::string currentSelectedName = (selectedFileIdx >= 0 && selectedFileIdx < (int)replayFiles.size())
                                                                  ? replayFiles[selectedFileIdx]
                                                                  : "";

                            replayFiles.clear();

                            // 2. Scan "Replays" folder
                            for (const auto &entry : fs::directory_iterator(folderPath))
                            {
                                if (entry.path().extension() == ".rec")
                                {
                                    replayFiles.push_back(entry.path().string());
                                }
                            }

                            // 3. Sort descending

                            std::sort(replayFiles.rbegin(), replayFiles.rend());

                            // 4. Restore selection index
                            selectedFileIdx = -1;
                            for (int i = 0; i < (int)replayFiles.size(); i++)
                            {
                                if (replayFiles[i] == currentSelectedName)
                                {
                                    selectedFileIdx = i;
                                    break;
                                }
                            }

                            lastScanTime = currentTime;
                        }

                        // 2. Select file via combo box
                        const char *preview = (selectedFileIdx >= 0) ? replayFiles[selectedFileIdx].c_str() : "Select Replay...";
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100.0f * scale);
                        if (ImGui::BeginCombo("##Files", preview))
                        {
                            ImGui::SetWindowFontScale(scale);
                            for (int i = 0; i < replayFiles.size(); i++)
                            {
                                bool isSelected = (selectedFileIdx == i);
                                if (ImGui::Selectable(replayFiles[i].c_str(), isSelected))
                                {
                                    selectedFileIdx = i;
                                    replayFilename = replayFiles[i];
                                }
                                if (isSelected)
                                {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::SameLine();

                        // 1. Apply reddish color for delete button warning
                        ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.0f, 0.6f, 0.6f));
                        if (PonUI::Button("DELETE", ImVec2(90 * scale, 0), PonUI::Kind::Stop))
                        {
                            if (selectedFileIdx >= 0 && selectedFileIdx < (int)replayFiles.size())
                            {
                                // Set flag to open popup
                                ImGui::SetWindowFontScale(scale);
                                ImGui::OpenPopup("Delete Confirmation");
                            }
                        }
                        ImGui::PopStyleColor();

                        // 2. Popup content
                        if (ImGui::BeginPopupModal("Delete Confirmation", NULL, ImGuiWindowFlags_AlwaysAutoResize))
                        {
                            ImGui::SetWindowFontScale(scale);
                            ImGui::Text("Are you sure you want to delete this replay?\nThis cannot be undone!\n\n");
                            PonUI::Separator();

                            if (PonUI::Button("OK", ImVec2(120 * scale, 0), PonUI::Kind::Stop))
                            {
                                // Physically delete and update list
                                if (std::remove(replayFiles[selectedFileIdx].c_str()) == 0)
                                {
                                    replayFiles.erase(replayFiles.begin() + selectedFileIdx);
                                    selectedFileIdx = -1;
                                    replayFilename = "";
                                }
                                ImGui::CloseCurrentPopup();
                            }

                            ImGui::SameLine();

                            if (PonUI::Button("CANCEL", ImVec2(120 * scale, 0)))
                            {
                                ImGui::CloseCurrentPopup();
                            }

                            ImGui::EndPopup();
                        }

                        ImGui::Spacing();

                        // 3. Action buttons
                        if (PonUI::Button("RECORD NEW", ImVec2(150 * scale, 0), PonUI::Kind::Go))
                        {
                            std::string currentRomName = "";

                            // 1. Traverse games map to identify name
                            int idx = 0;
                            // Loop through pairs
                            for (auto const &p : games)
                            {
                                if (idx == selectedGameIndex)
                                {
                                    currentRomName = p.second.name; // Get ROM name
                                    break;
                                }
                                idx++;
                            }

                            if (!currentRomName.empty())
                            {
                                // 2. Get current time (YYYYMMDDhhmmss)
                                time_t now = time(nullptr);
                                struct tm *tm_now = localtime(&now);
                                char timeStr[20];
                                strftime(timeStr, sizeof(timeStr), "%Y%m%d%H%M%S", tm_now);

                                // 3. Generate filename
                                std::string replayFolder = "Replays/";
                                std::string newReplayFile = replayFolder + currentRomName + "@" + timeStr + ".rec";

                                CreateDirectoryA(replayFolder.c_str(), NULL);

                                // 4. Create empty file
                                std::ofstream ofs(newReplayFile);
                                ofs.close();

                                // 5. Proceed to process startup
                                char szExePath[MAX_PATH];
                                GetModuleFileNameA(NULL, szExePath, MAX_PATH);

                                std::string cmd = "\"" + std::string(szExePath) + "\"";
                                cmd += " -record \"" + newReplayFile + "\"";
                                // cmd += " \"roms/" + currentRomName + ".zip\"";
                                cmd += " \"" + s_Dir + "/" + currentRomName + ".zip\"";

                                STARTUPINFOA si = {sizeof(si)};
                                PROCESS_INFORMATION pi;
                                if (CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
                                {
                                    CloseHandle(pi.hProcess);
                                    CloseHandle(pi.hThread);
                                    exitLaunch = false;
                                    exit = true;
                                    saveSettings = true;
                                }
                            }
                        }
                        ImGui::SameLine();

                        // Enable play button only when file is selected
                        bool hasSelection = (selectedFileIdx >= 0 && selectedFileIdx < replayFiles.size());
                        if (!hasSelection)
                            ImGui::BeginDisabled();

                        if (PonUI::Button("PLAY SELECTED", ImVec2(170 * scale, 0), PonUI::Kind::Primary, hasSelection))
                        {
                            if (selectedFileIdx >= 0 && !replayFilename.empty())
                            {
                                char szExePath[MAX_PATH];
                                GetModuleFileNameA(NULL, szExePath, MAX_PATH);

                                // 1. replayFilename contains filename
                                std::string fullPath = replayFilename;

                                // 2. Extract ROM name
                                // Find last '/'
                                size_t lastSlash = fullPath.find_last_of("/\\");
                                std::string pureFilename = (lastSlash != std::string::npos) ? fullPath.substr(lastSlash + 1) : fullPath;

                                // Find "@"
                                size_t pos = pureFilename.find('@');
                                std::string romName = (pos != std::string::npos) ? pureFilename.substr(0, pos) : "";

                                if (!romName.empty())
                                {
                                    // 3. Assemble command line
                                    // Pass fullPath
                                    std::string cmd = "\"" + std::string(szExePath) + "\"";
                                    cmd += " -play \"" + fullPath + "\"";
                                    cmd += " \"" + s_Dir + "/" + romName + ".zip\"";

                                    // 4. Start process
                                    STARTUPINFOA si = {sizeof(si)};
                                    PROCESS_INFORMATION pi;
                                    if (CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
                                    {
                                        CloseHandle(pi.hProcess);
                                        CloseHandle(pi.hThread);
                                        exitLaunch = false;
                                        exit = true;
                                        saveSettings = true;
                                    }
                                }
                            }
                        }

                        if (!hasSelection)
                            ImGui::EndDisabled();

                        PonUI::Text("REPLAY FILES GO IN THE 'REPLAYS' FOLDER.", PonUI::cFaint, std::max(1, psc - 1));
                    }
                    ImGui::EndChild();

                    ImGui::Spacing();
                    ImGui::Spacing();

                    // --- System Settings Section ---
                    PonUI::Text("SYSTEM", PonUI::cAccent);
                    PonUI::Separator();
                    ImGui::Spacing();
                    ImGui::SetWindowFontScale(scale);
                    ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.0f, 0.6f, 0.6f));
                    if (PonUI::Button("RESET ALL SETTINGS", ImVec2(260 * scale, 0), PonUI::Kind::Stop))
                    {
                        // Open confirmation popup
                        ImGui::OpenPopup("Reset Confirmation");
                    }
                    ImGui::PopStyleColor();
                    // Modal popup settings
                    if (ImGui::BeginPopupModal("Reset Confirmation", NULL, ImGuiWindowFlags_AlwaysAutoResize))
                    {
                        ImGui::SetWindowFontScale(scale);
                        ImGui::Text("This will delete your 'Config/Supermodel.ini' and close the app.\n"
                                    "All your preferences will be lost. Are you sure?\n\n");
                        PonUI::Separator();

                        // "Yes" button
                        if (PonUI::Button("YES, RESET EVERYTHING", ImVec2(260 * scale, 0), PonUI::Kind::Stop))
                        {
                            // Physical deletion
                            // Attempt deletion
                            std::remove("Config/Supermodel.ini");

                            // Exit without saving
                            exitLaunch = false;
                            exit = true;
                            saveSettings = false;

                            ImGui::CloseCurrentPopup();
                        }

                        ImGui::SameLine();

                        // "No" button
                        if (PonUI::Button("CANCEL", ImVec2(120 * scale, 0)))
                        {
                            ImGui::CloseCurrentPopup();
                        }

                        ImGui::EndPopup();
                    }
                    PonUI::Text("DELETES SUPERMODEL.INI AND CLOSES THE APP.", PonUI::cFaint, std::max(1, psc - 1));
                    // Position at the end of tabs

                }
                if (s_ponTab == 5)
                {
                    ImGui::SetWindowFontScale(scale);
                    float windowWidth = ImGui::GetContentRegionAvail().x;

                    // Margin top
                    ImGui::Dummy(ImVec2(0, 20.0f * scale));

                    // --- 1. Center Title ---
                    const char *title = "Supermodel-PonMi-Edition";
                    float titleWidth = ImGui::CalcTextSize(title).x;
                    ImGui::SetCursorPosX((windowWidth - titleWidth) * 0.5f);
                    ImGui::Text(title);

                    // --- 2. Center Version ---
                    const char *ver = "ver. 2026.07.31";
                    float verWidth = ImGui::CalcTextSize(ver).x;
                    ImGui::SetCursorPosX((windowWidth - verWidth) * 0.5f);
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), ver);

                    ImGui::Spacing();
                    PonUI::Separator();
                    ImGui::Spacing();

                    // --- 3. Center Message ---
                    const char *credit = "Developed by BackPonBeauty";
                    float creditWidth = ImGui::CalcTextSize(credit).x;
                    ImGui::SetCursorPosX((windowWidth - creditWidth) * 0.5f);
                    ImGui::Text(credit);

                    // --- 4. Center Buttons ---
                    float buttonWidth = 240.0f * scale;
                    ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);
                    if (PonUI::Button("VISIT GITHUB REPOSITORY", ImVec2(buttonWidth, 40 * scale), PonUI::Kind::Primary))
                    {
                        ShellExecuteA(NULL, "open", "https://github.com/BackPonBeauty", NULL, NULL, SW_SHOWNORMAL);
                    }
                    // --- Supporters Section ---
                    ImGui::Spacing();
                    PonUI::Separator();
                    ImGui::Spacing();

                    // 1. Get available width
                    float availWidth = ImGui::GetContentRegionAvail().x;

                    // 2. Center label text
                    const char *supportLabel = "Support the Developer:";
                    float labelSize = ImGui::CalcTextSize(supportLabel).x;
                    ImGui::SetCursorPosX((availWidth - labelSize) * 0.5f);
                    ImGui::Text(supportLabel);

                    // 3. Mock margins to center TextWrapped
                    // Use ChildWindow to center
                    float wrapWidth = 400.0f * scale; // Max wrap width
                    if (availWidth > wrapWidth)
                    {
                        ImGui::SetCursorPosX((availWidth - wrapWidth) * 0.5f);
                    }

                    ImGui::Spacing();

                    // 4. Center GitHub button
                    float btnWidth = 240.0f * scale;
                    ImGui::SetCursorPosX((availWidth - btnWidth) * 0.5f);

                    if (PonUI::Button("SPONSOR ON GITHUB", ImVec2(btnWidth, 40 * scale), PonUI::Kind::Go))
                    {
                        ShellExecuteA(NULL, "open", "https://github.com/sponsors/BackPonBeauty", NULL, NULL, SW_SHOWNORMAL);
                    }

                    // Tooltip
                    if (ImGui::IsItemHovered())
                    {
                        // Start rendering tooltip
                        ImGui::BeginTooltip();

                        // Scale tooltip font size
                        ImGui::SetWindowFontScale(scale);

                        // Display message
                        ImGui::Text("Fuel my development with some coffee!");

                        // End rendering tooltip
                        ImGui::EndTooltip();
                    }
                    ImGui::Spacing();
                    PonUI::Separator();
                    ImGui::Spacing();
                    // 1. Section Title
                    const char *thanksTitle = "[ Special Thanks ]";
                    float thanksTitleWidth = ImGui::CalcTextSize(thanksTitle).x;
                    ImGui::SetCursorPosX((availWidth - thanksTitleWidth) * 0.5f);
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), thanksTitle);

                    // 2. Supporters list
                    struct Gratitude
                    {
                        const char *category;
                        const char *name;
                    };
                    Gratitude thanksList[] = {
                        {"Official Supermodel3 Team", "Bart Trzynadlowski, Nik Henson"},
                        {"All Supermodel Developers", "and Fellow Fork Developers"},
                        {"Many critical comments", "@5ch people"},
                        {"Barrel Effect Inspired by", "@ikarugaginkei5744"},
                        {"Special Shout-out to", "all the 'Anti-PonMi' folks"}};

                    for (const auto &item : thanksList)
                    {
                        // Category
                        ImGui::SetWindowFontScale(scale);
                        float catWidth = ImGui::CalcTextSize(item.category).x;
                        ImGui::SetCursorPosX((availWidth - catWidth) * 0.5f);
                        // ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), item.category);
                        ImGui::Text(item.category);

                        // Name
                        ImGui::SetWindowFontScale(scale);
                        float nameWidth = ImGui::CalcTextSize(item.name).x;
                        ImGui::SetCursorPosX((availWidth - nameWidth) * 0.5f);
                        ImGui::Text(item.name);

                        ImGui::Spacing();
                    }

                    ImGui::Dummy(ImVec2(0, 10.0f * scale));

                }
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndGroup();

    // --- Bottom: Control Panel ---
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5.0f * scale));
    if (ImGui::BeginChild("FooterBar", ImVec2(0, 0), false))
    {
        ImGui::SetWindowFontScale(scale); // Re-apply scale

        float btnWidth = 220.0f * scale;
        float btnHeight = -1.0f; // Expand button height
        float spacing = 10.0f * scale;

        if (PonUI::Button("LAUNCH", ImVec2(btnWidth, btnHeight), PonUI::Kind::Go))
        {
            if (CheckRomExists(selectedGameIndex, games, config))
            {
                saveSettings = true;
                // 11/24 (owner): Spikeout too starts at once - the lobby is only for a start with -lobby (Main.cpp)
                exitLaunch = true;
                exit = true;
            }
            else
            {
                printf("[LAUNCH] ROM not found, launch canceled\n");
            }
        }
        ImGui::SameLine(0, spacing);
        // std::string Dir = config["Dir"].ValueAs<std::string>();
        //  --- Add ROM path setting area ---
        // 2. Folder selection button
        if (PonUI::Button("DIR...", ImVec2(90.0f * scale, -1)))
        {
            std::string pickedPath = "";

#ifdef _WIN32
            // --- Windows Process ---
            BROWSEINFO bi = {0};
            bi.lpszTitle = "Select ROM Directory";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
            if (pidl != nullptr)
            {
                char path[MAX_PATH];
                if (SHGetPathFromIDList(pidl, path))
                    pickedPath = path;
                CoTaskMemFree(pidl);
            }
            /*
    #else

            // --- Linux Process (Zenity) ---
            // Zenity is standard on Linux
            FILE* f = popen("zenity --file-selection --directory --title=\"Select ROM Directory\"", "r");
            if (f) {
                char buffer[1024];
                if (fgets(buffer, sizeof(buffer), f)) {
                    pickedPath = buffer;
                    // Remove trailing newline
                    pickedPath.erase(pickedPath.find_last_not_of("\n\r") + 1);
                }
                pclose(f);
            }
            */

            if (!pickedPath.empty())
            {
                // Convert to '/'
                for (auto &c : pickedPath)
                {
                    if (c == '\\')
                        c = '/';
                }
                // config.Set("Dir", pickedPath);
                s_Dir = pickedPath;
            }
        }
#endif
        ImGui::SameLine(0, spacing);

        // std::string Dir = config["Dir"].ValueAs<std::string>();
        // std::string Dir = config.Get("Supermodel3UI").Get("Dir").ValueAs<std::string>();
        std::replace(s_Dir.begin(), s_Dir.end(), '\\', '/');

        // Work buffer for ImGui edit
        char pathBuf[512];
        strncpy(pathBuf, s_Dir.c_str(), sizeof(pathBuf));
        pathBuf[sizeof(pathBuf) - 1] = '\0';

        // Calculate textbox width
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btnWidth - spacing - 10.0f);

        // Pass char array
        if (ImGui::InputText("##RomPathStr", pathBuf, sizeof(pathBuf)))
        {
            std::string newPath = std::string(pathBuf);
            // Update config "Dir"
            std::replace(newPath.begin(), newPath.end(), '\\', '/');
            // config.Set("Dir", std::string(pathBuf));
            s_Dir = newPath;
        }
        // ----------------------------

        ImGui::SameLine(ImGui::GetWindowWidth() - btnWidth - 10.0f);

        if (PonUI::Button("EXIT", ImVec2(btnWidth, btnHeight), PonUI::Kind::Stop))
        {
            exitLaunch = false;
            exit = true;
            saveSettings = true;
        }
    }
    ImGui::EndChild();

    // Described in GUI main loop
    if (g_PreviewWindow)
    {
        // Get preview window ID
        uint32_t previewID = SDL_GetWindowID(g_PreviewWindow);
        // Get currently focused window
        SDL_Window *focusWin = SDL_GetMouseFocus();

        // Get left click state
        if (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT))
        {
            // If mouse is over preview window
            // Dismiss popup
            if (focusWin == g_PreviewWindow)
            {
                ClosePreviewWindow();
                // SDL_DestroyWindow(g_PreviewWindow);
                // g_PreviewWindow = nullptr;
            }
        }
    }

    const uint8_t *state = SDL_GetKeyboardState(NULL);

    if (state[SDL_SCANCODE_ESCAPE])
    {

        exitLaunch = false;

        exit = true;

        saveSettings = true;
    }

    ImGui::End();
}

// --- Entry Point ---
std::vector<std::string> RunGUI(const std::string &configPath, Util::Config::Node &config)
{
    s_launchFromLobby = false;
    if (!resLoaded)
    {
        std::ifstream file("Resolution.txt");
        std::string line;
        if (file.is_open())
        {
            while (std::getline(file, line))
            {
                if (!line.empty())
                {
                    resolutions.push_back(line);
                }
            }
            file.close();
        }
        else
        {
            // Fallback when file is missing
            resolutions.push_back("640x480");
            resolutions.push_back("1920x1080");
        }
        resLoaded = true;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0)
        return {};

    // --- [1] Read from Supermodel.ini (Before loop) ---

    // Graphics
    int engineSelection = config["New3DEngine"].ValueAs<bool>() ? 0 : 1;
    bool vVsync = config["VSync"].ValueAs<bool>();
    bool vQuadRendering = config["QuadRendering"].ValueAs<bool>();
    bool vGPUMultiThreaded = config["GPUMultiThreaded"].ValueAs<bool>();
    bool vMultiThreaded = config["MultiThreaded"].ValueAs<bool>();
    bool vMultiTexture = config["MultiTexture"].ValueAs<bool>();
    bool vBorderless = config["BorderlessWindow"].ValueAs<bool>();
    bool vTrueAR = config["true-ar"].ValueAs<bool>();
    bool vOverlay = config["Overlay"].ValueAs<bool>();
    bool vFullScreen = config["FullScreen"].ValueAs<bool>();
    bool vWideScreen = config["WideScreen"].ValueAs<bool>();
    bool vWideBackground = config["WideBackground"].ValueAs<bool>();
    bool vStretch = config["Stretch"].ValueAs<bool>();
    bool vShowFrameRate = config["ShowFrameRate"].ValueAs<bool>();
    bool vThrottle = config["Throttle"].ValueAs<bool>();
    bool vNoWhiteFlash = config["NoWhiteFlash"].ValueAs<bool>();
    bool vHideCMD = config["HideCMD"].ValueAs<bool>();
    bool vDefaultScanline = config["DefaultScanline"].ValueAs<bool>();
    float vRefreshRate = config["RefreshRate"].ValueAs<float>();
    bool vTrueHz;
    if (vRefreshRate == 60.0f)
    {
        vTrueHz = false;
    }
    else
    {
        vTrueHz = true;
    }

    int superSampling = (int)config["Supersampling"].ValueAs<int64_t>();
    int selectedCRT = (int)config["CRTcolors"].ValueAs<int64_t>();
    int selectedUpscale = (int)config["UpscaleMode"].ValueAs<int64_t>();
    int ppcFreq = (int)config["PowerPCFrequency"].ValueAs<int64_t>();
    int WindowXPosition = (int)config["WindowXPosition"].ValueAs<int64_t>();
    int WindowYPosition = (int)config["WindowYPosition"].ValueAs<int64_t>();
    // 11/24 (owner): the boxes start from the ini (the position the game window was last closed at). They were "0"
    // until the Video tab was opened, so saving the settings (also on LAUNCH) wrote 0,0 over the remembered position.
    // An empty ini value stays empty (= centred).
    {
        const bool hasX = config.TryGet("WindowXPosition") && !config["WindowXPosition"].Empty();
        const bool hasY = config.TryGet("WindowYPosition") && !config["WindowYPosition"].Empty();
        if (hasX) snprintf(bufPosX, sizeof(bufPosX), "%d", WindowXPosition); else bufPosX[0] = '\0';
        if (hasY) snprintf(bufPosY, sizeof(bufPosY), "%d", WindowYPosition); else bufPosY[0] = '\0';
    }
    int Scanline = (int)config["ScanlineStrength"].ValueAs<int64_t>();
    int Barrel = (int)config["BarrelStrength"].ValueAs<int64_t>();

    // Sound
    int musicVol = (int)config["MusicVolume"].ValueAs<int64_t>();
    int sfxVol = (int)config["SoundVolume"].ValueAs<int64_t>();
    int balance = (int)config["Balance"].ValueAs<int64_t>();
    bool vEmulateSound = config["EmulateSound"].ValueAs<bool>();
    bool vEmulateDSB = config["EmulateDSB"].ValueAs<bool>();
    bool vFlipStereo = config["FlipStereo"].ValueAs<bool>();
    bool vLegacySoundDSP = config["LegacySoundDSP"].ValueAs<bool>();

    // Control (Select index by string comparison)
    std::string inSys = config["InputSystem"].ValueAs<std::string>();
    int selectedInputType = (inSys == "xinput") ? 0 : (inSys == "dinput") ? 1
                                                  : (inSys == "rawinput") ? 2
                                                  : (inSys == "sdl" || inSys == "sdlgamepad") ? 3
                                                  : (inSys == "none" || inSys == "NONE") ? 4   // P3: streaming server
                                                                          : 2;   // 11/11: "sdl" was read as Rawinput

    int selectedCrosshair = (int)config["Crosshairs"].ValueAs<int64_t>();

    std::string styleStr = config["CrosshairStyle"].ValueAs<std::string>();
    int selectedStyle = (styleStr == "vector") ? 0 : 1;
    bool vForceFeedback = config["ForceFeedback"].ValueAs<bool>();

    // Network
    bool vNetwork = config["Network"].ValueAs<bool>();
    bool vSimulateNet = config["SimulateNet"].ValueAs<bool>();
    bool vStreaming = config["Streaming"].ValueAsDefault<bool>(false);
    std::string vCodec = "H265";
    if (config.TryGet("Codec") && !config["Codec"].Empty()) {
        vCodec = config["Codec"].ValueAs<std::string>();
    } else if (config.TryGet("Decoder") && !config["Decoder"].Empty()) {
        vCodec = config["Decoder"].ValueAs<std::string>();
    } else if (config.TryGet("Decorder") && !config["Decorder"].Empty()) {
        vCodec = config["Decorder"].ValueAs<std::string>();
    }

    // Network strings
    strncpy(bufPortIn, config["PortIn"].ValueAs<std::string>().c_str(), sizeof(bufPortIn) - 1);
    bufPortIn[sizeof(bufPortIn) - 1] = '\0';
    strncpy(bufPortOut, config["PortOut"].ValueAs<std::string>().c_str(), sizeof(bufPortOut) - 1);
    bufPortOut[sizeof(bufPortOut) - 1] = '\0';
    strncpy(bufAddressOut, config["AddressOut"].ValueAs<std::string>().c_str(), sizeof(bufAddressOut) - 1);
    bufAddressOut[sizeof(bufAddressOut) - 1] = '\0';
    s_Dir = config["Dir"].ValueAs<std::string>();

    struct GuiSettings
    {
        int engineSelection;
        bool vVsync, vQuadRendering, vGPUMultiThreaded, vMultiThreaded;
        bool vMultiTexture, vBorderless, vTrueAR, vOverlay, vFullScreen, vWideScreen;
        bool vWideBackground, vStretch, vShowFrameRate, vThrottle, vNoWhiteFlash, vHideCMD, vDefaultScanline, vTrueHz;
        int superSampling, selectedCRT, selectedUpscale, ppcFreq;
        int WindowXPosition, WindowYPosition, Scanline, Barrel;
        int musicVol, sfxVol, balance;
        bool vEmulateSound, vEmulateDSB, vFlipStereo, vLegacySoundDSP;
        int selectedInputType, selectedCrosshair, selectedStyle;
        bool vForceFeedback, vNetwork, vSimulateNet;
        int selectedResIndex;
    };

    SDL_Window *window = SDL_CreateWindow("Supermodel-PonMi-Edition", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 600, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
    if (!window)
        return {};

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    glewExperimental = GL_TRUE;
    glewInit();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();

    ImFontConfig font_cfg;

    // --- Add: Read SelectedGameIdx manually ---
    int selectedGame = -1;
    bool startMaximized = false;
    float fontSize = 16.0f;
    {
        std::ifstream ifs("ponmi.ini");
        if (ifs.is_open())
        {
            std::string line;
            while (std::getline(ifs, line))
            {
                // Find "Key=Value"
                size_t sep = line.find('=');
                if (sep == std::string::npos)
                    continue;

                std::string key = line.substr(0, sep);
                std::string val = line.substr(sep + 1);

                if (key == "SelectedGameIdx")
                {
                    selectedGame = std::stoi(val);
                }
                else if (key == "Maximized")
                {
                    startMaximized = (val == "1"); // Check boolean value
                }
                else if (key == "FontSize")
                {
                    fontSize = std::stof(val);
                }
                else if (key == "ImageAreaRatioW")
                {
                    uImageAreaRatioW = std::stof(val);
                }
                else if (key == "ImageAreaRatioH")
                {
                    uImageAreaRatioH = std::stof(val);
                }
            }
        }
        ifs.close();
    }

    // 11/24 (owner): every text not in the game's font is in a Windows font (Yu Gothic: Japanese too). The old
    // pixel font (Font01) and the Japanese glyphs merged into it only when no Windows font is there.
    if (!PonUI::AddWindowsFont(fontSize))
    {
    font_cfg.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF((void *)Font01_data, sizeof(Font01_data), fontSize, &font_cfg);

    // Streaming lobby (P6b, 2026-11-16): Discord names and host names may be Japanese. Merge the kana/kanji
    // glyphs of a Windows font into the GUI font (skipped when none of these files exists).
    {
        static const char *jpFonts[] = { "C:\\Windows\\Fonts\\meiryo.ttc", "C:\\Windows\\Fonts\\YuGothM.ttc", "C:\\Windows\\Fonts\\msgothic.ttc" };
        for (const char *path : jpFonts)
        {
            FILE *fp = fopen(path, "rb");
            if (!fp)
                continue;
            fclose(fp);
            ImFontConfig jp;
            jp.MergeMode = true;
            jp.PixelSnapH = true;
            // 11/24 (owner): the Japanese glyphs sat lower than the pixel font's letters ("背中ポン美 TEST")
            jp.GlyphOffset = ImVec2(0.0f, -fontSize * 0.20f);
            io.Fonts->AddFontFromFileTTF(path, fontSize, &jp, io.Fonts->GetGlyphRangesJapanese());
            break;
        }
    }
    }
    // 11/24 (owner): the lobby's server names in a Windows font (Yu Gothic Bold, else Meiryo / MS Gothic)
    PonUI::SetSystemFont(PonUI::AddWindowsFont(fontSize, true));

    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 150");
    PonUI::Init();   // 11/24: PonNvram's look (colours, the game's font)

    uint32_t window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (startMaximized)
    {
        SDL_MaximizeWindow(window);
    }

    // --- Add: Restore size and position from ini ---
    ImGui::LoadIniSettingsFromDisk(io.IniFilename);

    // Direct calculation
    ImGuiID canvasID = ImHashStr("LauncherCanvas");
    ImGuiWindowSettings *settings = ImGui::FindWindowSettingsByID(canvasID);

    if (settings)
    {
        int savedW = (int)settings->Size.x;
        int savedH = (int)settings->Size.y;
        int savedX = (int)settings->Pos.x;
        int savedY = (int)settings->Pos.y;

        // Apply size
        if (savedW > 0 && savedH > 0)
        {
            SDL_SetWindowSize(window, savedW, savedH);
        }

        // Apply position
        if (savedX != 0 || savedY != 0)
        {
            SDL_SetWindowPosition(window, savedX, savedY);
        }
        else
        {
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
    }
    // --- End ---

    GameLoader loader(config["GameXMLFile"].ValueAs<std::string>());
    auto &games = loader.GetGames();

    bool saveSettings = true, running = true, exit = false, exitLaunch = false;

    SDL_ShowWindow(window);

    int selectedResIndex = 0;
    // Get resolution string
    std::string currentRes = std::to_string(config["XResolution"].ValueAs<int64_t>()) + "x" +
                             std::to_string(config["YResolution"].ValueAs<int64_t>());

    // Find match in resolutions list
    for (int i = 0; i < (int)resolutions.size(); i++)
    {
        if (resolutions[i] == currentRes)
        {
            selectedResIndex = i;
            break;
        }
    }

    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
            {
                running = false;
                exit = false;
                saveSettings = false;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Streaming (P5): the lobby replaces the settings screen while it is open
        if (Lobby::IsOpen())
        {
            Lobby::SetRomFile((std::filesystem::path(s_Dir) / "spikeofe.zip").string());   // 11/24: NETLINK needs it
            Lobby::Action action = Lobby::Draw(1.0f);
            if (action == Lobby::Action::Connect)
            {
                Lobby::Close();
                // no ROM launch: Main runs the stream client (Lobby::Request()); 11/23 a NETLINK slot launches the ROM
                // (Main joins as that slot's clone)
                exitLaunch = Lobby::Request().netlink;
                s_launchFromLobby = exitLaunch;
                exit = true;
            }
            else if (action == Lobby::Action::Skip)
            {
                Lobby::Close();
                exitLaunch = true;      // CL-02b: the normal local spikeofe
                s_launchFromLobby = true;
                exit = true;
            }
            else if (action == Lobby::Action::Back)
            {
                // 11/24 (owner): the lobby's EXIT closes the program, as the settings screen's EXIT does
                Lobby::Close();
                exitLaunch = false;
                exit = true;
            }
        }
        else
        // GUI(io, config, games, selectedGame, exit, exitLaunch, saveSettings, window,selectedResIndex);
        GUI(io, config, games, selectedGame, exit, exitLaunch, saveSettings, window,
            selectedResIndex, engineSelection, vVsync, vQuadRendering, vGPUMultiThreaded,
            vMultiThreaded, vMultiTexture, vBorderless, vTrueAR, vOverlay, vFullScreen,
            vWideScreen, vWideBackground, vStretch, vShowFrameRate, vThrottle,
            vNoWhiteFlash, vHideCMD, vDefaultScanline, vTrueHz, superSampling, selectedCRT, selectedUpscale, ppcFreq, WindowXPosition, WindowYPosition, Scanline, Barrel,
            musicVol, sfxVol, balance, vEmulateSound, vEmulateDSB, vFlipStereo,
            vLegacySoundDSP, selectedInputType, selectedCrosshair, selectedStyle,
            vForceFeedback, vNetwork, vSimulateNet, vStreaming, vCodec);
        if (exit)
        {
            running = false;
        }

        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.02f, 0.03f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // --- Post-termination judgment ---
    std::vector<std::string> roms;

    // Return ROM path on LAUNCH button click
    if (exitLaunch)
    {
        // Get default ROM path. 11/24: from the lobby (SKIP, a NETLINK slot) it is always Spikeout's ROM, whatever
        // game the list has chosen (the lobby can be opened at start with -lobby, before any choice)
        int romGame = selectedGame;
        if (s_launchFromLobby)
        {
            int idx = 0;
            for (auto const &pair : games)
            {
                if (pair.second.name == "spikeofe")
                {
                    romGame = idx;
                    break;
                }
                idx++;
            }
        }
        std::string path = GetRomPath(romGame, games, config);
        if (!path.empty())
        {
            roms.emplace_back(path);
        }
    }

    // Cleanup
    if (g_GameTexture)
        glDeleteTextures(1, &g_GameTexture);
    PonUI::Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();

    std::string iniData;
    if (saveSettings)
    {
        std::map<std::string, std::string> u;

        // --- Video Settings ---
        u["New3DEngine"] = (engineSelection == 0 ? "1" : "0");
        u["VSync"] = (vVsync ? "1" : "0");
        u["QuadRendering"] = (vQuadRendering ? "1" : "0");
        u["GPUMultiThreaded"] = (vGPUMultiThreaded ? "1" : "0");
        u["MultiThreaded"] = (vMultiThreaded ? "1" : "0");
        u["MultiTexture"] = (vMultiTexture ? "1" : "0");
        u["BorderlessWindow"] = (vBorderless ? "1" : "0");
        u["FullScreen"] = (vFullScreen ? "1" : "0");
        u["WideScreen"] = (vWideScreen ? "1" : "0");
        u["WideBackground"] = (vWideBackground ? "1" : "0");
        u["Stretch"] = (vStretch ? "1" : "0");
        u["ShowFrameRate"] = (vShowFrameRate ? "1" : "0");
        u["Throttle"] = (vThrottle ? "1" : "0");
        u["NoWhiteFlash"] = (vNoWhiteFlash ? "1" : "0");
        u["HideCMD"] = (vHideCMD ? "1" : "0");
        u["DefaultScanline"] = (vDefaultScanline ? "1" : "0");
        u["true-ar"] = (vTrueAR ? "1" : "0");
        u["Overlay"] = (vOverlay ? "1" : "0");

        // Refresh rate
        std::stringstream ss;
        ss << std::fixed << std::setprecision(6) << (vTrueHz ? 57.524158 : 60.0);
        u["RefreshRate"] = ss.str();

        // --- Numerical values ---
        // 1. Get selected resolution string
        std::string resStr = resolutions[selectedResIndex];
        int resW = 0, resH = 0;

        // 2. Split string to w and h
        if (sscanf(resStr.c_str(), "%dx%d", &resW, &resH) == 2)
        {
            // Insert into save map
            u["XResolution"] = std::to_string(resW);
            u["YResolution"] = std::to_string(resH);
        }
        else
        {
            // Fallback on parse failure
            u["XResolution"] = std::to_string(XResolution);
            u["YResolution"] = std::to_string(YResolution);
        }

        // Copy value
        u["WindowXPosition"] = bufPosX;
        u["WindowYPosition"] = bufPosY;
        u["Supersampling"] = std::to_string(superSampling);
        u["CRTcolors"] = std::to_string(selectedCRT);
        u["UpscaleMode"] = std::to_string(selectedUpscale);
        u["PowerPCFrequency"] = std::to_string(ppcFreq);
        u["ScanlineStrength"] = std::to_string(Scanline);
        u["BarrelStrength"] = std::to_string(Barrel);

        // --- Sound Settings ---
        u["MusicVolume"] = std::to_string(musicVol);
        u["SoundVolume"] = std::to_string(sfxVol);
        u["Balance"] = std::to_string(balance);
        u["EmulateSound"] = (vEmulateSound ? "1" : "0");
        u["EmulateDSB"] = (vEmulateDSB ? "1" : "0");
        u["FlipStereo"] = (vFlipStereo ? "1" : "0");
        u["LegacySoundDSP"] = (vLegacySoundDSP ? "1" : "0");

        // --- Control / Network ---
        u["InputSystem"] = (selectedInputType == 0) ? "xinput" : (selectedInputType == 1) ? "dinput"
                         : (selectedInputType == 3) ? (inSys == "sdlgamepad" ? "sdlgamepad" : "sdl")   // 11/11: SDL was saved as rawinput
                         : (selectedInputType == 4) ? "NONE"                                           // P3: streaming server
                                                                                          : "rawinput";

        u["Crosshairs"] = std::to_string(selectedCrosshair);
        u["CrosshairStyle"] = (selectedStyle == 0) ? "vector" : "bmp";
        u["ForceFeedback"] = (vForceFeedback ? "1" : "0");
        u["Network"] = (vNetwork ? "1" : "0");
        u["SimulateNet"] = (vSimulateNet ? "1" : "0");
        u["Streaming"] = (vStreaming ? "1" : "0");
        u["Codec"] = vCodec;
        u["Decoder"] = vCodec;
        u["Decorder"] = vCodec;
        // Keep linkplay
        u["PortIn"] = bufPortIn;
        u["PortOut"] = bufPortOut;
        u["AddressOut"] = bufAddressOut;
        u["Dir"] = s_Dir;

        // Call custom update function
        SaveSupermodelConfig(configPath, u);
    }

    ImGui::DestroyContext();
    // Check settings save
    // --- Inside post-termination judgment ---
    if (saveSettings)
    {
        // Save ImGui settings to imgui.ini
        // Create ponmi.ini
        std::ofstream ofs("ponmi.ini", std::ios::trunc);
        uint32_t flags = SDL_GetWindowFlags(window);
        bool isMaximized = (flags & SDL_WINDOW_MAXIMIZED);
        if (ofs.is_open())
        {
            ofs << "[Settings]\n";
            ofs << "SelectedGameIdx=" << selectedGame << "\n";
            ofs << "Maximized=" << isMaximized << "\n";
            ofs << "FontSize=" << fontSize << "\n";
            ofs << "ImageAreaRatioW=" << uImageAreaRatioW << "\n";
            ofs << "ImageAreaRatioH=" << uImageAreaRatioH << "\n";
            // Expandable settings
            ofs.close();
            saveSettings = false;
        }
    }

    SDL_GL_DeleteContext(glContext);

    SDL_DestroyWindow(window);
    SDL_Quit();

    return roms;
}
