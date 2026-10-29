#include "ts4_settings.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <windows.h>

#include "regpu/regpu.h"
#include "user_dir.h"
#include "log.h"

namespace
{
Ts4Settings g_settings;
bool g_loaded = false;

Ts4Settings::Upscaler ParseUpscaler(const std::string& v)
{
    if (v == "fsr1")
        return Ts4Settings::Upscaler::Fsr1;
    return Ts4Settings::Upscaler::Off;
}

const char* UpscalerName(Ts4Settings::Upscaler u)
{
    switch (u)
    {
    case Ts4Settings::Upscaler::Fsr1: return "fsr1";
    default: return "off";
    }
}
} // namespace

std::filesystem::path Ts4SettingsIniPath()
{
    return DocumentsUserDir() / "settings.ini";
}

void Ts4Settings::RenderSize(uint32_t& w, uint32_t& h) const
{
    w = width;
    h = height;
    uint64_t aw = aspectW, ah = aspectH;
    if (!aw || !ah)
        aw = width, ah = height;
    if (aw * 9 < ah * 16)
        aw = 16, ah = 9;
    if (uint64_t(width) * ah > uint64_t(height) * aw)
        w = uint32_t(uint64_t(height) * aw / ah) & ~1u;
    else
        h = uint32_t(uint64_t(width) * ah / aw) & ~1u;
}

float Ts4Settings::SceneScale() const
{
    if (upscaler == Upscaler::Off)
        return 1.0f;
    return float(renderScale) / 100.0f;
}

Ts4Settings& GetTs4Settings()
{
    if (!g_loaded)
    {
        LoadTs4Settings();
    }
    return g_settings;
}

void LoadTs4Settings()
{
    g_loaded = true;
    const std::filesystem::path ini = Ts4SettingsIniPath();
    std::ifstream in(ini);
    std::string line;
    while (std::getline(in, line))
    {
        const size_t eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#' || line[0] == ';')
            continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);

        if (key == "profile" && !value.empty())
            g_settings.profile = value.substr(0, 15);
        else if (key == "resolution")
            sscanf(value.c_str(), "%ux%u", &g_settings.width, &g_settings.height);
        else if (key == "aspect")
        {
            if (sscanf(value.c_str(), "%u:%u", &g_settings.aspectW, &g_settings.aspectH) != 2)
                g_settings.aspectW = g_settings.aspectH = 0;
        }
        else if (key == "fullscreen")
            g_settings.fullscreen = (value == "1");
        else if (key == "msaa")
            g_settings.msaa = uint32_t(atoi(value.c_str()));
        else if (key == "upscaler")
            g_settings.upscaler = ParseUpscaler(value);
        else if (key == "render_scale")
            g_settings.renderScale = uint32_t(atoi(value.c_str()));
        else if (key == "sharpness")
            g_settings.sharpness = float(atof(value.c_str()));
        else if (key == "skip_intro")
            g_settings.skipIntro = (value != "0");
        else if (key == "fast_geometry")
            g_settings.fastGeometry = (value == "1");
        else if (key == "game_dir" && !value.empty())
            g_settings.gameDir = value;
        else if (key == "language")
            g_settings.language = uint32_t(atoi(value.c_str()));
        else if (key == "patch_repo" && !value.empty())
            g_settings.patchRepo = value;
        else if (key == "patch_branch" && !value.empty())
            g_settings.patchBranch = value;
    }

    // Overrides from environment variables
    const auto getEnvStr = [](const char* name1, const char* name2 = nullptr) -> const char* {
        if (const char* v = getenv(name1)) return v;
        if (name2) { if (const char* v2 = getenv(name2)) return v2; }
        return nullptr;
    };

    if (const char* v = getEnvStr("TS4_PROFILE", "FM2_PROFILE"))
        g_settings.profile = std::string(v).substr(0, 15);
    if (const char* v = getEnvStr("TS4_RESOLUTION", "FM2_RESOLUTION"))
        sscanf(v, "%ux%u", &g_settings.width, &g_settings.height);
    if (const char* v = getEnvStr("TS4_FULLSCREEN", "FM2_FULLSCREEN"))
        g_settings.fullscreen = (atoi(v) != 0);
    if (const char* v = getEnvStr("TS4_MSAA", "FM2_MSAA"))
        g_settings.msaa = uint32_t(atoi(v));
    if (const char* v = getEnvStr("TS4_UPSCALER", "FM2_UPSCALER"))
        g_settings.upscaler = ParseUpscaler(v);
    if (const char* v = getEnvStr("TS4_RENDER_SCALE", "FM2_FSR_SCALE"))
        g_settings.renderScale = uint32_t(atoi(v));
    if (const char* v = getEnvStr("TS4_SHARPNESS", "FM2_SHARPNESS"))
        g_settings.sharpness = float(atof(v));
    if (const char* v = getenv("TS4_SKIP_INTRO"))
        g_settings.skipIntro = (atoi(v) != 0);
    if (const char* v = getenv("TS4_FAST_GEOMETRY"))
        g_settings.fastGeometry = (atoi(v) != 0);
    if (const char* v = getenv("TS4_GAME_DIR"))
        g_settings.gameDir = v;
    if (const char* v = getEnvStr("TS4_LANGUAGE", "FM2_LANGUAGE"))
        g_settings.language = uint32_t(atoi(v));

    // Clamp values
    g_settings.width = std::clamp<uint32_t>(g_settings.width, 640, 15360);
    g_settings.height = std::clamp<uint32_t>(g_settings.height, 360, 8640);
    g_settings.msaa = g_settings.msaa >= 4 ? 4 : g_settings.msaa >= 2 ? 2 : 1;
    g_settings.renderScale = std::clamp<uint32_t>(g_settings.renderScale, 50, 100);
    g_settings.sharpness = std::clamp(g_settings.sharpness, 0.0f, 1.0f);
    if (g_settings.profile.empty())
        g_settings.profile = "Player";
    if (!g_settings.language)
        g_settings.language = 1;
}

void SaveTs4Settings()
{
    const Ts4Settings& s = GetTs4Settings();
    const std::filesystem::path ini = Ts4SettingsIniPath();
    std::error_code ec;
    std::filesystem::create_directories(ini.parent_path(), ec);

    std::ofstream out(ini);
    out << "# Top Spin 4 (TS4) settings - edited by the launcher.\n";
    out << "profile=" << s.profile << "\n";
    out << "resolution=" << s.width << "x" << s.height << "\n";
    out << "aspect=" << (s.aspectW ? std::to_string(s.aspectW) + ":" + std::to_string(s.aspectH) : std::string("auto")) << "\n";
    out << "fullscreen=" << (s.fullscreen ? 1 : 0) << "\n";
    out << "msaa=" << s.msaa << "\n";
    out << "upscaler=" << UpscalerName(s.upscaler) << "\n";
    out << "render_scale=" << s.renderScale << "\n";
    out << "sharpness=" << s.sharpness << "\n";
    out << "skip_intro=" << (s.skipIntro ? 1 : 0) << "\n";
    out << "fast_geometry=" << (s.fastGeometry ? 1 : 0) << "\n";
    out << "game_dir=" << s.gameDir.string() << "\n";
    out << "language=" << s.language << "\n";
    out << "patch_repo=" << s.patchRepo << "\n";
    out << "patch_branch=" << s.patchBranch << "\n";
}

void ApplyTs4SettingsToRuntime()
{
    const Ts4Settings& s = GetTs4Settings();
    regpu::Config& cfg = regpu::GetConfig();

    cfg.profile = s.profile;
    cfg.width = s.width;
    cfg.height = s.height;
    cfg.aspectW = s.aspectW;
    cfg.aspectH = s.aspectH;
    cfg.fullscreen = s.fullscreen;
    cfg.msaa = s.msaa;
    cfg.upscaler = (s.upscaler == Ts4Settings::Upscaler::Fsr1) ? regpu::Config::Upscaler::Fsr1 : regpu::Config::Upscaler::Off;
    cfg.sharpness = s.sharpness;
    cfg.language = s.language;

    // TS4 paces on 60 Hz vblank: keep internal vsync=false and maxFps=1000
    cfg.vsync = false;
    cfg.maxFps = 1000;

    // Apply skip intro to environment
    SetEnvironmentVariableA("TS4_SKIP_INTRO", s.skipIntro ? "1" : "0");

    // "Faster geometry (experimental)":
    // When unchecked (default), disable direct fetch and direct index to prevent black tiling
    // during camera pans.
    if (!s.fastGeometry)
    {
        SetEnvironmentVariableA("REGPU_NO_DIRECT_FETCH", "1");
        SetEnvironmentVariableA("REGPU_NO_DIRECT_INDEX", "1");
    }
    else
    {
        // If user explicitly checked faster geometry, ensure direct fetch/index are allowed
        // unless externally specified.
        if (!getenv("REGPU_FORCE_NO_DIRECT"))
        {
            SetEnvironmentVariableA("REGPU_NO_DIRECT_FETCH", nullptr);
            SetEnvironmentVariableA("REGPU_NO_DIRECT_INDEX", nullptr);
        }
    }

    Log("settings: profile '%s', %ux%u, aspect %s, %s, msaa %ux, upscaler %s, skip_intro %d, fast_geometry %d",
        s.profile.c_str(), s.width, s.height,
        s.aspectW ? (std::to_string(s.aspectW) + ":" + std::to_string(s.aspectH)).c_str() : "auto",
        s.fullscreen ? "fullscreen" : "windowed",
        s.msaa, UpscalerName(s.upscaler), s.skipIntro ? 1 : 0, s.fastGeometry ? 1 : 0);
}

