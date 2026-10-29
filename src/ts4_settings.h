#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

struct Ts4Settings
{
    std::string profile = "Player";
    uint32_t width = 1280, height = 720;
    uint32_t aspectW = 0, aspectH = 0; // 0 = Auto
    bool fullscreen = false;
    uint32_t msaa = 1; // 1 (Off), 2, 4
    enum class Upscaler : uint32_t { Off, Fsr1 };
    Upscaler upscaler = Upscaler::Off;
    uint32_t renderScale = 100;
    float sharpness = 0.5f;
    bool skipIntro = true;
    bool fastGeometry = false; // "Faster geometry (experimental)" - default false
    std::filesystem::path gameDir = "game";
    uint32_t language = 1;
    std::string patchRepo = "zer0ducksgiven/topspin4-patches";
    std::string patchBranch = "ts4-patches";

    void RenderSize(uint32_t& w, uint32_t& h) const;
    float SceneScale() const;
};

Ts4Settings& GetTs4Settings();
void LoadTs4Settings();
void SaveTs4Settings();
void ApplyTs4SettingsToRuntime();
std::filesystem::path Ts4SettingsIniPath();

