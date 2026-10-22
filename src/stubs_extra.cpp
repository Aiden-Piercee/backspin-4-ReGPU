#include <cstdint>
#include <filesystem>

uint32_t g_binkRingBase = 0;

void AudioNoteMusicRead(const std::filesystem::path& file, uint64_t offset, const uint8_t* data, uint32_t bytes)
{
}

void AudioInit()
{
}

bool RunLauncher()
{
    return true; // Proceed directly to game startup
}
