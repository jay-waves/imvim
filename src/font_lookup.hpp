#pragma once
#include <filesystem>
#include <string>
namespace imvim {
struct FontFace { std::filesystem::path file; int index = 0; };
FontFace find_system_font(const std::string& family);
}
