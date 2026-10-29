#pragma once
#include <filesystem>

// Shows the Top Spin 4 launcher UI.
// Returns true to proceed to game startup; false if user quit.
// Updates gameDir if the user chose or imported another game directory.
bool RunLauncher(std::filesystem::path& gameDir);

