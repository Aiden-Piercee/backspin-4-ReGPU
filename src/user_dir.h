#pragma once
#include <filesystem>

// The user folder: "user" next to the executable (or TS4_USER_DIR), see user_dir.cpp.
const std::filesystem::path& DocumentsUserDir();
