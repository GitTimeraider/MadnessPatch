#pragma once

#include <d3d9.h>
#include <Windows.h>
#include <wincodec.h>
#include <Xinput.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include "Common.hpp"
#include "Features.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"
#include "imgui/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace AchievementOverlay
{
    // Config
    inline constexpr bool kBlockGameInputWhileVisible = true;

    // State
    inline IDirect3DDevice9* g_pDevice = nullptr;
    inline IDirect3DDevice9* g_pInitializedDevice = nullptr;
    inline HWND g_hWnd = nullptr;
    inline WNDPROC g_oWndProc = nullptr;
    inline uintptr_t g_devicePtrAddr = 0;

    inline bool g_hooksInstalled = false;
    inline bool g_imguiInitialized = false;
    inline bool g_inReset = false;
    inline bool g_deviceLost = false;
    inline bool g_visible = false;

    inline float g_uiScale = 1.0f;
    inline float g_pendingScale = 0.0f;

    inline int g_lastDispW = 0;
    inline int g_lastDispH = 0;

    inline float g_savedScrollY = 0.0f;
    inline bool g_restoreScroll = false;

    inline XINPUT_STATE g_padState{};
    inline bool g_padConnected = false;

    struct ToastItem
    {
        int achvId;
        unsigned long long start;
        unsigned int uid;
        float height;
        float animY;
        bool placed;
    };
    inline std::vector<ToastItem> g_toasts;
    inline unsigned int g_toastNextId = 1;

    // Achievement data
    struct Texture
    {
        IDirect3DTexture9* tex = nullptr;
        int w = 0;
        int h = 0;
    };

    struct AchievementText
    {
        std::string name;
        std::string desc;
    };

    inline std::vector<Texture> g_achievements;
    inline std::vector<bool> g_unlocked;
    inline std::vector<int> g_current;
    inline std::vector<AchievementText> g_text;

    inline bool g_achievementsLoaded = false;
    inline bool g_textLoaded = false;
    inline std::string g_language = "en";

    inline std::filesystem::path g_achievementFolder;
    inline std::filesystem::path g_achievementFilePath;
    inline uint64_t g_unlockedBits = 0; // persistent unlock bitflags
    inline std::unordered_map<int, int> g_maxOverride; // runtime max overrides read from the game

    inline constexpr int kAchievementCount = 45;
    inline float g_ventDuration = 0.0f; // persisted steam-vent timer
    inline bool g_ventNeedsApply = false; // game-side writes g_ventDuration back into the engine once on load

    inline bool g_showSecrets = false;

    // Secret achievements hidden until unlocked
    inline const std::vector<int> kSecretIndices = { 1, 2, 3, 4, 5, 6, 7, 13, 14, 15, 16, 17, 18, 19, 31, 33, 35 };

    inline bool IsSecret(int index)
    {
        for (int s : kSecretIndices)
        {
            if (s == index)
            {
                return true;
            }
        }
        return false;
    }

    // Target count per achievement, anything not listed is a plain on/off unlock (max 1)
    inline const std::unordered_map<int, int> kAchievementMax =
    {
        { 19, 4 }, { 20, 4 }, { 21, 16 }, { 22, 34 }, { 23, 94 }, { 34, 10 },
        { 35, 420 }, { 38, 10 }, { 39, 30 }, { 42, 100 }, { 43, 52 },
    };

    inline int AchievementMax(int index)
    {
        auto ov = g_maxOverride.find(index);
        if (ov != g_maxOverride.end())
        {
            return ov->second;
        }

        auto it = kAchievementMax.find(index);
        return it != kAchievementMax.end() ? it->second : 1;
    }

    inline bool IsVisible() { return g_visible; }

    inline void SetAchievementUnlocked(int index, bool unlocked)
    {
        if (index < 0) return;

        if ((int)g_unlocked.size() <= index)
        {
            g_unlocked.resize(index + 1, false);
        }

        g_unlocked[index] = unlocked;
    }

    // Marks the achievement unlocked with no toast or save, used by the save-load sync
    inline bool MarkUnlockedSilent(int achvId)
    {
        if (achvId < 0 || achvId >= 64)
            return false;

        const uint64_t bit = 1ull << achvId;
        if (g_unlockedBits & bit)
            return false;

        g_unlockedBits |= bit;
        SetAchievementUnlocked(achvId, true);
        return true;
    }

    // Queue an unlock toast
    inline void PushToast(int achvId)
    {
        ToastItem it{};
        it.achvId = achvId;
        it.start = GetTickCount64();
        it.uid = g_toastNextId++;
        g_toasts.push_back(it);
    }

    // Persist the unlock flags and steam-vent timer to Achievements.txt
    inline void SaveAchievementBits()
    {
        if (g_achievementFilePath.empty())
            return;

        std::ofstream f(g_achievementFilePath, std::ios::trunc);
        if (f)
        {
            f << "UnlockFlag=" << g_unlockedBits << "\n" << "TotalVentDuration=" << g_ventDuration << "\n";
        }
    }

    // Index 0 is the platinum, award it once every other trophy is unlocked
    inline void AwardPlatinumIfComplete()
    {
        if (g_unlockedBits & 1ull)
            return;

        uint64_t allOthers = 0;
        for (int i = 1; i < kAchievementCount; i++)
        {
            allOthers |= (1ull << i);
        }

        if ((g_unlockedBits & allOthers) == allOthers)
        {
            g_unlockedBits |= 1ull;
            SetAchievementUnlocked(0, true);
            PushToast(0);

            SaveAchievementBits();
        }
    }

    // Load the unlock flags + steam-vent timer for a profile's save folder
    inline void InitAchievementFile(const std::filesystem::path& folder)
    {
        if (folder == g_achievementFolder)
            return;

        g_achievementFolder = folder;
        g_achievementFilePath = folder / L"Achievements.txt";

        std::error_code ec;
        if (!std::filesystem::exists(g_achievementFilePath, ec))
        {
            std::filesystem::create_directories(folder, ec);
            std::ofstream create(g_achievementFilePath); // create an empty Achievements.txt
        }

        g_unlockedBits = 0;
        g_ventDuration = 0.0f;
        {
            std::ifstream f(g_achievementFilePath);
            std::string line;
            while (std::getline(f, line))
            {
                const size_t eq = line.find('=');
                if (eq == std::string::npos)
                    continue;

                const std::string key = line.substr(0, eq);
                const char* val = line.c_str() + eq + 1;
                if (key == "UnlockFlag")
                {
                    g_unlockedBits = std::strtoull(val, nullptr, 10);

                }
                else if (key == "TotalVentDuration")
                {
                    g_ventDuration = std::strtof(val, nullptr);
                }
            }
        }
        g_ventNeedsApply = true; // game-side restores g_ventDuration into the engine on the next tick

        if ((int)g_unlocked.size() < 64)
        {
            g_unlocked.resize(64, false);
        }
        for (int i = 0; i < 64; i++)
        {
            g_unlocked[i] = ((g_unlockedBits >> i) & 1ull) != 0;
        }

        AwardPlatinumIfComplete(); // a save that already holds every non-platinum trophy
    }

    // Marks the achievement unlocked, saves, and shows a toast only if it wasn't already unlocked
    inline bool NotifyUnlock(int achvId)
    {
        if (achvId < 0 || achvId >= 64)
            return false;

        const uint64_t bit = 1ull << achvId;
        if (g_unlockedBits & bit)
            return false;

        g_unlockedBits |= bit;
        SetAchievementUnlocked(achvId, true);
        PushToast(achvId);
        SaveAchievementBits();

        if (achvId != 0)
        {
            AwardPlatinumIfComplete();
        }

        return true;
    }

    // Persist the current steam-vent timer (called when Alice leaves a vent).
    inline void SaveVentDuration(float seconds)
    {
        g_ventDuration = seconds;
        SaveAchievementBits();
    }

    // Runtime max for counters whose total is read from the game
    inline void SetAchievementMax(int index, int maxValue)
    {
        if (index < 0) return;
        g_maxOverride[index] = maxValue;
    }

    // Current progress for an achievement
    inline void SetAchievementProgress(int index, int current)
    {
        if (index < 0) return;

        if ((int)g_current.size() <= index)
        {
            g_current.resize(index + 1, 0);
        }
        if (current < 0)
        {
            current = 0;
        }

        g_current[index] = current;
    }

    // Clear all unlock/progress state so everything shows as locked again, not called automatically
    inline void ResetProgress()
    {
        g_unlockedBits = 0;
        g_unlocked.assign(g_unlocked.size(), false);
        g_current.assign(g_current.size(), 0);
    }

    // Reloads achievements\txt\<lang>.txt on the next frame
    inline void SetLanguage(const char* langCode)
    {
        if (!langCode || !langCode[0])
            return;

        g_language = langCode;
        g_textLoaded = false;
    }

    // Helpers
    inline IDirect3DDevice9* GetDevice()
    {
        if (g_devicePtrAddr == 0)
            return nullptr;

        return *reinterpret_cast<IDirect3DDevice9**>(g_devicePtrAddr);
    }

    // Initial scale guess from the backbuffer, the runtime scale from io.DisplaySize.y corrects it
    inline float ComputeUiScale(IDirect3DDevice9* dev)
    {
        float renderHeight = 1080.0f;
        IDirect3DSurface9* bb = nullptr;
        if (dev && SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
        {
            D3DSURFACE_DESC d{};
            if (SUCCEEDED(bb->GetDesc(&d)))
            {
                renderHeight = (float)d.Height;
            }

            bb->Release();
        }

        return (renderHeight > 1080.0f ? renderHeight / 1080.0f : 1.0f);
    }

    // Texture & text loading
    inline bool LoadTextureFromFile(IDirect3DDevice9* dev, const char* path, Texture& out, int targetSize = 0)
    {
        static bool s_comInit = false;
        if (!s_comInit)
        {
            HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;
            s_comInit = true;
        }

        wchar_t wpath[MAX_PATH];
        if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH) == 0)
        {
            return false;
        }

        IWICImagingFactory* factory = nullptr;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        {
            return false;
        }

        bool ok = false;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICBitmapScaler* scaler = nullptr;
        IWICFormatConverter* conv = nullptr;
        IDirect3DTexture9* tex = nullptr;

        if (SUCCEEDED(factory->CreateDecoderFromFilename(wpath, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) && SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&conv)))
        {
            // Scale first, then convert to BGRA
            IWICBitmapSource* src = frame;
            if (targetSize > 0 && SUCCEEDED(factory->CreateBitmapScaler(&scaler)) && SUCCEEDED(scaler->Initialize(frame, targetSize, targetSize, WICBitmapInterpolationModeFant)))
            {
                src = scaler;
            }

            if (SUCCEEDED(conv->Initialize(src, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
            {
                UINT w = 0, h = 0;
                conv->GetSize(&w, &h);
                if (w > 0 && h > 0 && SUCCEEDED(dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)))
                {
                    D3DLOCKED_RECT rect{};
                    if (SUCCEEDED(tex->LockRect(0, &rect, nullptr, 0)))
                    {
                        // CopyPixels honors the destination stride, so it fills the locked rect even when Pitch != w*4.
                        HRESULT cp = conv->CopyPixels(nullptr, (UINT)rect.Pitch, (UINT)rect.Pitch * h, (BYTE*)rect.pBits);
                        tex->UnlockRect(0);
                        if (SUCCEEDED(cp))
                        {
                            out.tex = tex;
                            out.w = (int)w;
                            out.h = (int)h;
                            ok = true;
                        }
                    }
                }
            }
        }

        if (!ok && tex) tex->Release();
        if (conv) conv->Release();
        if (scaler) scaler->Release();
        if (frame) frame->Release();
        if (decoder) decoder->Release();
        factory->Release();
        return ok;
    }

    inline void ReleaseAchievements()
    {
        for (auto& a : g_achievements)
        {
            if (a.tex)
            {
                a.tex->Release();
            }
        }

        g_achievements.clear();
        g_achievementsLoaded = false;
    }

    inline void LoadAchievements(IDirect3DDevice9* dev)
    {
        if (g_achievementsLoaded || !dev)
            return;

        g_achievementsLoaded = true;

        int target = (int)(64.0f * g_uiScale + 0.5f); // icons draw at 64 * g_uiScale, load them ~1:1

        std::string dir = SystemHelper::GetModulePath() + "\\achievements\\img\\";
        for (int i = 0; ; i++)
        {
            std::string p = dir + std::to_string(i) + ".png";
            if (GetFileAttributesA(p.c_str()) == INVALID_FILE_ATTRIBUTES)
            {
                break;
            }

            Texture t;
            LoadTextureFromFile(dev, p.c_str(), t, target); // keep index alignment even if a load fails
            g_achievements.push_back(t);

            if ((int)g_unlocked.size() <= i) g_unlocked.push_back(false);
            if ((int)g_current.size() <= i) g_current.push_back(0);
        }
    }

    inline void LoadText()
    {
        if (g_textLoaded) return;

        g_textLoaded = true;
        g_text.clear();

        std::string path = SystemHelper::GetModulePath() + "\\achievements\\txt\\" + g_language + ".txt";

        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "rb") != 0 || !f)
        {
            return;
        }

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        std::string buf;
        if (size > 0)
        {
            buf.resize(size);
            fread(&buf[0], 1, size, f);
        }

        fclose(f);

        size_t pos = 0;
        if (buf.size() >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF)
        {
            pos = 3; // skip UTF-8 BOM
        }

        while (pos < buf.size())
        {
            size_t eol = buf.find('\n', pos);
            std::string line = (eol == std::string::npos) ? buf.substr(pos) : buf.substr(pos, eol - pos);
            pos = (eol == std::string::npos) ? buf.size() : eol + 1;

            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (line.empty())
            {
                continue;
            }

            AchievementText at;
            size_t bar = line.find('|');
            if (bar == std::string::npos)
            {
                at.name = line;
            }
            else
            {
                at.name = line.substr(0, bar);
                at.desc = line.substr(bar + 1);
            }

            g_text.push_back(at);
        }
    }

    // ImGui setup
    inline void InitImGui(IDirect3DDevice9* pDevice)
    {
        if (g_imguiInitialized || !pDevice || !g_hWnd)
            return;

        g_uiScale = (g_pendingScale > 0.0f) ? g_pendingScale : ComputeUiScale(pDevice);

        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;

        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::StyleColorsDark();
        style.ScaleAllSizes(g_uiScale);
        style.FontScaleDpi = g_uiScale; // scales the font to the render resolution
        style.AntiAliasedLinesUseTex = false; // we render with point sampling, which the textured-line path can't use

        if (g_uiScale < 1.15f)
        {
            io.Fonts->AddFontDefaultBitmap();
        }
        else
        {
            io.Fonts->AddFontDefaultVector();
        }

        ImGui_ImplWin32_Init(g_hWnd);
        ImGui_ImplDX9_Init(pDevice);

        g_imguiInitialized = true;
        g_pInitializedDevice = pDevice;
    }

    inline void ShutdownImGui()
    {
        if (!g_imguiInitialized) return;

        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        g_imguiInitialized = false;
        g_pInitializedDevice = nullptr;
    }

    // Input
    inline float GetControllerScrollAxis()
    {
        if (!g_padConnected) return 0.0f;

        const short dz = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
        short ly = g_padState.Gamepad.sThumbLY;

        if (ly > dz)
            return (float)(ly - dz) / (float)(32767 - dz);
        if (ly < -dz)
            return (float)(ly + dz) / (float)(32768 - dz);

        return 0.0f;
    }

    // Rendering
    inline void DrawAchievementsWindow(IDirect3DDevice9* pDevice)
    {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 disp = io.DisplaySize;

        ImVec2 winSize(disp.x * 0.6f, disp.y * 0.8f);
        winSize.x = winSize.x < 700.0f ? 700.0f : (winSize.x > disp.x - 40.0f ? disp.x - 40.0f : winSize.x);
        winSize.y = winSize.y < 500.0f ? 500.0f : (winSize.y > disp.y - 40.0f ? disp.y - 40.0f : winSize.y);

        bool resChanged = ((int)disp.x != g_lastDispW) || ((int)disp.y != g_lastDispH);
        g_lastDispW = (int)disp.x;
        g_lastDispH = (int)disp.y;

        ImGuiCond sizeCond = resChanged ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        ImGui::SetNextWindowSize(winSize, sizeCond);
        ImGui::SetNextWindowPos(ImVec2(disp.x * 0.5f, disp.y * 0.5f), sizeCond, ImVec2(0.5f, 0.5f));

        float axis = GetControllerScrollAxis();
        float wheel = ImGui::GetIO().MouseWheel;

        if (g_restoreScroll)
        {
            ImGui::SetNextWindowScroll(ImVec2(-1.0f, g_savedScrollY));
        }

        if (ImGui::Begin("Achievements", &g_visible))
        {
            static bool s_secretPrev = false;
            bool secretNow = g_padConnected && (g_padState.Gamepad.wButtons & XINPUT_GAMEPAD_A);

            if (secretNow && !s_secretPrev)
            {
                g_showSecrets = !g_showSecrets;
            }

            s_secretPrev = secretNow;

            if (g_restoreScroll)
            {
                if (axis > 0.5f || axis < -0.5f || wheel != 0.0f)
                {
                    g_restoreScroll = false;
                }
            }
            else if (axis != 0.0f)
            {
                float delta = axis * 1500.0f * g_uiScale * ImGui::GetIO().DeltaTime;
                ImGui::SetScrollY(ImGui::GetScrollY() - delta);
            }

            ImGui::Checkbox("Show secret achievements", &g_showSecrets);
            ImGui::Separator();

            const float iconSize = 64.0f * g_uiScale;

            for (size_t i = 0; i < g_achievements.size(); i++)
            {
                ImGui::PushID((int)i);

                const Texture& t = g_achievements[i];
                bool unlocked = (i < g_unlocked.size()) ? g_unlocked[i] : false;
                int maxv = AchievementMax((int)i);
                int cur = (i < g_current.size()) ? g_current[i] : 0;

                bool reveal = !IsSecret((int)i) || g_showSecrets || unlocked;

                ImVec4 tint = unlocked ? ImVec4(1, 1, 1, 1) : ImVec4(0.35f, 0.35f, 0.35f, 1.0f);

                if (reveal && t.tex)
                {
                    ImGui::ImageWithBg((ImTextureID)(uintptr_t)t.tex, ImVec2(iconSize, iconSize), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tint);
                }
                else
                {
                    // Hidden placeholder box with a centered "?"
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImVec2 p2(p.x + iconSize, p.y + iconSize);
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(p, p2, IM_COL32(90, 90, 90, 255), 4.0f);
                    dl->AddRectFilled(ImVec2(p.x + 1, p.y + 1), ImVec2(p2.x - 1, p2.y - 1), IM_COL32(35, 35, 35, 255), 4.0f);
                    ImVec2 ts = ImGui::CalcTextSize("?");
                    dl->AddText(ImVec2(p.x + (iconSize - ts.x) * 0.5f, p.y + (iconSize - ts.y) * 0.5f), IM_COL32(170, 170, 170, 255), "?");
                    ImGui::Dummy(ImVec2(iconSize, iconSize));
                }

                ImGui::SameLine();
                ImGui::BeginGroup();

                const char* name = "???";
                const char* desc = "Hidden achievement";
                if (reveal)
                {
                    name = (i < g_text.size()) ? g_text[i].name.c_str() : "???";
                    desc = (i < g_text.size()) ? g_text[i].desc.c_str() : "";
                }

                ImVec4 nameCol = unlocked ? ImVec4(1.0f, 0.84f, 0.40f, 1.0f) : ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
                ImGui::TextColored(nameCol, "%s", name);

                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
                ImGui::TextWrapped("%s", desc);
                ImGui::PopTextWrapPos();

                char overlay[32];
                float barFrac;
                if (!reveal)
                {
                    // Hidden achievement: don't reveal any progress
                    barFrac = 0.0f;
                    snprintf(overlay, sizeof(overlay), "???");
                }
                else
                {
                    // Counter-backed achievements show their stored value; binary ones use the unlock flag
                    bool hasCounter = maxv > 1;
                    int displayCur = hasCounter ? cur : (unlocked ? 1 : 0);
                    barFrac = maxv > 0 ? (float)displayCur / (float)maxv : (unlocked ? 1.0f : 0.0f);
                    if (barFrac > 1.0f) barFrac = 1.0f;
                    snprintf(overlay, sizeof(overlay), "%d/%d", displayCur, maxv);
                }

                // Draw the bar without its built-in overlay, then place the counter at the top-left
                ImVec2 barPos = ImGui::GetCursorScreenPos();
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.22f, 0.60f, 0.85f, 1.0f));
                ImGui::ProgressBar(barFrac, ImVec2(-FLT_MIN, 0.0f), "");
                ImGui::PopStyleColor();

                ImVec2 ots = ImGui::CalcTextSize(overlay);
                float barH = ImGui::GetFrameHeight();
                ImGui::GetWindowDrawList()->AddText(ImVec2(barPos.x + 6.0f * g_uiScale, barPos.y + (barH - ots.y) * 0.5f), IM_COL32(255, 255, 255, 255), overlay);

                ImGui::EndGroup();
                ImGui::PopID();

                ImGui::Separator();
            }

            if (!g_restoreScroll)
            {
                g_savedScrollY = ImGui::GetScrollY();
            }
        }

        ImGui::End();
    }

    inline void DrawUnlockToast()
    {
        if (g_toasts.empty()) return;

        const float kSlideMs = 350.0f;
        const float kHoldMs = 5500.0f;
        const float kTotalMs = 6200.0f;

        unsigned long long now = GetTickCount64();

        for (size_t i = 0; i < g_toasts.size(); )
        {
            if ((float)(now - g_toasts[i].start) >= kTotalMs)
            {
                g_toasts.erase(g_toasts.begin() + i);
            }
            else
            {
                i++;
            }
        }

        if (g_toasts.empty()) return;

        ImGuiIO& io = ImGui::GetIO();
        float scale = g_uiScale;
        float toastW = 420.0f * scale;
        float margin = 20.0f * scale;
        float spacing = 8.0f * scale;
        float onX = io.DisplaySize.x - toastW - margin;
        float offX = io.DisplaySize.x + 10.0f;

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs;

        float targetY = margin;
        for (ToastItem& it : g_toasts)
        {
            float t = (float)(now - it.start);

            float x, alpha;
            if (t < kSlideMs)
            {
                float k = t / kSlideMs;
                k = 1.0f - (1.0f - k) * (1.0f - k); // ease-out
                x = offX + (onX - offX) * k;
                alpha = k;
            }
            else if (t < kHoldMs)
            {
                x = onX;
                alpha = 1.0f;
            }
            else
            {
                float k = (t - kHoldMs) / (kTotalMs - kHoldMs);
                x = onX + (offX - onX) * (k * k); // ease-in
                alpha = 1.0f - k;
            }

            // Ease the vertical slot so cards slide up when one above expires
            if (!it.placed)
            {
                it.animY = targetY; it.placed = true;
            }
            else
            {
                it.animY += (targetY - it.animY) * 0.25f;
            }

            ImGui::SetNextWindowPos(ImVec2(x, it.animY));
            ImGui::SetNextWindowSize(ImVec2(toastW, 0.0f));

            char wname[32];
            snprintf(wname, sizeof(wname), "##toast_%u", it.uid);

            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
            if (ImGui::Begin(wname, nullptr, flags))
            {
                int id = it.achvId;
                float iconSize = 64.0f * scale;

                if (id >= 0 && id < (int)g_achievements.size() && g_achievements[id].tex)
                {
                    ImGui::ImageWithBg((ImTextureID)(uintptr_t)g_achievements[id].tex, ImVec2(iconSize, iconSize), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 1));
                    ImGui::SameLine();
                }

                ImGui::BeginGroup();
                ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.40f, 1.0f), "Achievement Unlocked");
                const char* name = (id >= 0 && id < (int)g_text.size()) ? g_text[id].name.c_str() : "";
                const char* desc = (id >= 0 && id < (int)g_text.size()) ? g_text[id].desc.c_str() : "";
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
                ImGui::TextWrapped("%s", name);
                if (desc[0])
                {
                    ImGui::TextColored(ImVec4(0.78f, 0.78f, 0.78f, 1.0f), "%s", desc);
                }

                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
            }

            it.height = ImGui::GetWindowHeight();
            ImGui::End();
            ImGui::PopStyleVar();

            targetY += (it.height > 1.0f ? it.height : 90.0f * scale) + spacing;
        }
    }

    inline void Render(IDirect3DDevice9* pDevice)
    {
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();

        ImGuiIO& io = ImGui::GetIO();

        float clientW = io.DisplaySize.x;
        float clientH = io.DisplaySize.y;

        float renderW = clientW, renderH = clientH;
        IDirect3DSurface9* bb = nullptr;
        if (SUCCEEDED(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
        {
            D3DSURFACE_DESC d{};
            if (SUCCEEDED(bb->GetDesc(&d)) && d.Width > 0 && d.Height > 0)
            {
                renderW = (float)d.Width;
                renderH = (float)d.Height;
            }

            bb->Release();
        }

        io.DisplaySize = ImVec2(renderW, renderH);

        g_pendingScale = (renderH > 1080.0f ? renderH / 1080.0f : 1.0f);

        if (clientW > 0.0f && clientH > 0.0f && (renderW != clientW || renderH != clientH))
        {
            POINT pt;
            if (GetCursorPos(&pt) && ScreenToClient(g_hWnd, &pt))
            {
                io.AddMousePosEvent((float)pt.x * (renderW / clientW), (float)pt.y * (renderH / clientH));
            }
        }

        ImGui::NewFrame();

        if (ImDrawCallback setNearest = ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest)
        {
            ImGui::GetBackgroundDrawList()->AddCallback(setNearest, nullptr);
        }

        if (g_visible)
        {
            DrawAchievementsWindow(pDevice);
        }

        DrawUnlockToast();

        ImGui::EndFrame();
        ImGui::Render();

        pDevice->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
        pDevice->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
        pDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        pDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        pDevice->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }

    inline LRESULT CALLBACK WndProc_Hook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (g_imguiInitialized && g_visible && !g_deviceLost)
        {
            ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);

            if (kBlockGameInputWhileVisible)
            {
                switch (msg)
                {
                    case WM_KEYUP: case WM_SYSKEYUP:
                    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP:
                        break;

                    case WM_SYSKEYDOWN:
                        if (wParam == VK_F4) break;
                        return TRUE;

                    case WM_MOUSEMOVE:
                    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
                    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
                    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
                    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
                    case WM_KEYDOWN: case WM_CHAR:
                        return TRUE;

                    default: break;
                }
            }
        }

        return CallWindowProc(g_oWndProc, hWnd, msg, wParam, lParam);
    }

    // Device
    inline bool InstallHooks(IDirect3DDevice9* pDevice)
    {
        if (g_hooksInstalled || !pDevice)
            return g_hooksInstalled;

        D3DDEVICE_CREATION_PARAMETERS cp{};
        if (FAILED(pDevice->GetCreationParameters(&cp)) || !cp.hFocusWindow)
        {
            return false;
        }

        g_pDevice = pDevice;
        g_hWnd = cp.hFocusWindow;

        g_oWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc_Hook)));

        g_hooksInstalled = true;
        return true;
    }

    inline bool PollDeviceHealth(IDirect3DDevice9* pDevice)
    {
        if (!pDevice) return false;

        HRESULT coop = pDevice->TestCooperativeLevel();
        if (coop != D3D_OK)
        {
            if (g_imguiInitialized && !g_deviceLost)
            {
                ImGui_ImplDX9_InvalidateDeviceObjects();
                g_deviceLost = true;
            }

            return false;
        }

        if (g_imguiInitialized && (g_deviceLost || g_inReset))
        {
            ImGui_ImplDX9_CreateDeviceObjects();
            g_deviceLost = false;
            g_inReset = false;
        }

        return true;
    }

    // Public API
    inline void Init(uintptr_t devicePtrAddr)
    {
        g_devicePtrAddr = devicePtrAddr;
    }

    inline void FeedControllerState(const XINPUT_STATE& state, bool connected)
    {
        g_padState = state;
        g_padConnected = connected;
    }

    inline bool WantCaptureController()
    {
        return kBlockGameInputWhileVisible && g_imguiInitialized && g_visible && !g_deviceLost;
    }

    inline void OnPresent(IDirect3DDevice9* pDevice)
    {
        if (!pDevice)
            return;

        if (!g_hooksInstalled)
        {
            InstallHooks(pDevice);
        }

        if (!PollDeviceHealth(pDevice))
        {
            return;
        }

        if (g_imguiInitialized && pDevice != g_pInitializedDevice)
        {
            ReleaseAchievements();
            ShutdownImGui();
        }

        else if (g_imguiInitialized && g_pendingScale > 0.0f && ((g_pendingScale > g_uiScale ? g_pendingScale - g_uiScale : g_uiScale - g_pendingScale) > 0.01f))
        {
            ReleaseAchievements(); // resolution changed: reload icons and re-bake the font
            ShutdownImGui();
        }

        if (!g_imguiInitialized)
        {
            InitImGui(pDevice);
        }

        if (g_imguiInitialized)
        {
            LoadAchievements(pDevice);
            LoadText();
        }

        if (!g_imguiInitialized || (!g_visible && g_toasts.empty()))
            return;

        ImGui::GetIO().MouseDrawCursor = g_visible; // cursor only for the menu, not toasts

        IDirect3DStateBlock9* stateBlock = nullptr;
        if (FAILED(pDevice->CreateStateBlock(D3DSBT_ALL, &stateBlock)))
        {
            stateBlock = nullptr;
        }

        if (stateBlock)
        {
            stateBlock->Capture();
        }

        IDirect3DSurface9* prevRT = nullptr;
        IDirect3DSurface9* bb = nullptr;

        D3DVIEWPORT9 prevViewport{};
        RECT prevScissor{};
        const bool haveViewport = SUCCEEDED(pDevice->GetViewport(&prevViewport));
        const bool haveScissor = SUCCEEDED(pDevice->GetScissorRect(&prevScissor));

        if (SUCCEEDED(pDevice->GetRenderTarget(0, &prevRT)) && prevRT && SUCCEEDED(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb && SUCCEEDED(pDevice->SetRenderTarget(0, bb)))
        {
            DWORD prevColorWrite = 0;
            pDevice->GetRenderState(D3DRS_COLORWRITEENABLE, &prevColorWrite);
            pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);

            HRESULT sceneHr = pDevice->BeginScene(); // if already open, skip our EndScene below
            Render(pDevice);

            if (SUCCEEDED(sceneHr))
            {
                pDevice->EndScene();
            }

            pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, prevColorWrite);
            pDevice->SetRenderTarget(0, prevRT);

            if (haveViewport)
            {
                pDevice->SetViewport(&prevViewport);
            }

            if (haveScissor)
            {
                pDevice->SetScissorRect(&prevScissor);
            }
        }

        if (stateBlock)
        {
            stateBlock->Apply();
            stateBlock->Release();
        }

        if (bb)
        {
            bb->Release();
        }

        if (prevRT)
        {
            prevRT->Release();
        }
    }

    inline void OnPresent()
    {
        OnPresent(GetDevice());
    }

    inline void OnDeviceLost()
    {
        g_inReset = true;
        if (g_imguiInitialized)
        {
            ImGui_ImplDX9_InvalidateDeviceObjects();
        }
    }

    inline void OnDeviceReset(IDirect3DDevice9* pDevice)
    {
        if (g_imguiInitialized)
        {
            ImGui_ImplDX9_CreateDeviceObjects();
        }

        g_inReset = false;
    }

    inline void OnDeviceReset()
    {
        OnDeviceReset(GetDevice());
    }

    inline void Toggle()
    {
        g_visible = !g_visible;

        if (g_visible)
        {
            g_restoreScroll = true;
        }
    }

    inline void Update(IDirect3DDevice9* pDevice)
    {
        if (!g_hooksInstalled)
        {
            InstallHooks(pDevice);
        }

        PollDeviceHealth(pDevice);

        if (GetAsyncKeyState(VK_HOME) & 1)
        {
            Toggle();
        }
    }
}