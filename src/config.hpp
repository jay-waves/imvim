#pragma once
#include <filesystem>
#include <string>
namespace imvim {
struct Config {
    std::string font, fallback_font;
    float font_size = 16, line_spacing = 0, letter_spacing = 0;
};
std::filesystem::path default_config_path();
Config load_config(const std::filesystem::path& path);
}
