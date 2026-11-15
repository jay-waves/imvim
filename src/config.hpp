#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace imvim {
struct Config {
    std::string font, fallback_font, font_family;
    std::vector<std::string> fallback_families;
    float cjk_scale = 1;
    int window_width = 1000, window_height = 700;
    bool maximized = false;
    std::string nvim_path = "nvim";
    std::vector<std::string> nvim_args;
    float font_size = 16, line_spacing = 0, letter_spacing = 0;
};
std::filesystem::path default_config_path();
Config load_config(const std::filesystem::path& path);
}
