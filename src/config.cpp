#include "config.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>
#include <cmath>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif
namespace imvim {
namespace {
std::filesystem::path home_path() {
#ifdef _WIN32
    DWORD size = GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
    if (size) {
        std::wstring value(size, 0);
        DWORD length = GetEnvironmentVariableW(L"USERPROFILE", value.data(), size);
        if (length && length < size) { value.resize(length); return value; }
    }
#else
    if (const char* value = std::getenv("HOME"); value && *value) return value;
#endif
    throw std::runtime_error("Cannot determine home directory for imvim configuration");
}
std::string font_path(const nlohmann::json& value, const std::filesystem::path& directory) {
    auto text = value.get<std::string>();
    if (text.empty()) return {};
    std::filesystem::path path(std::u8string(text.begin(), text.end()));
    if (text.starts_with("~/") || text.starts_with("~\\")) path = home_path() / std::filesystem::path(std::u8string(text.begin()+2,text.end()));
    else if (path.is_relative()) path = directory / path;
    auto bytes = path.lexically_normal().u8string(); return {bytes.begin(), bytes.end()};
}
void number(const nlohmann::json& json, const char* key, float& destination, float low, float high) {
    if (!json.contains(key)) return;
    if (!json.at(key).is_number()) throw std::runtime_error(std::string(key) + " must be a number");
    destination = json.at(key).get<float>();
    if (!std::isfinite(destination) || destination < low || destination > high)
        throw std::runtime_error(std::string(key) + " must be between " + std::to_string(low) + " and " + std::to_string(high));
}
}
std::filesystem::path default_config_path() { return home_path() / ".config" / "imvim" / "config.json"; }
Config load_config(const std::filesystem::path& path) {
    Config config;
    if (!std::filesystem::exists(path)) return config;
    try {
        if (std::filesystem::file_size(path) > 65536) throw std::runtime_error("Configuration exceeds 64 KiB");
        std::ifstream stream(path);
        if (!stream) throw std::runtime_error("Cannot open configuration");
        auto json = nlohmann::json::parse(stream);
        if (!json.is_object()) throw std::runtime_error("Configuration must be an object");
        for (const auto& entry : json.items()) if (entry.key() != "font") throw std::runtime_error("Unknown setting: " + entry.key());
        if (!json.contains("font")) return config;
        const auto& font = json.at("font");
        if (!font.is_object()) throw std::runtime_error("font must be an object");
        for (const auto& entry : font.items()) {
            auto key = entry.key();
            if (key != "file" && key != "fallback_file" && key != "size" && key != "line_spacing" && key != "letter_spacing")
                throw std::runtime_error("Unknown font setting: " + key);
        }
        if (font.contains("file")) config.font = font_path(font.at("file"), path.parent_path());
        if (font.contains("fallback_file")) config.fallback_font = font_path(font.at("fallback_file"), path.parent_path());
        number(font, "size", config.font_size, 8, 72);
        number(font, "line_spacing", config.line_spacing, -2, 32);
        number(font, "letter_spacing", config.letter_spacing, -2, 16);
        return config;
    } catch (const std::exception& e) {
        auto u8 = path.u8string();
        throw std::runtime_error("Config " + std::string(u8.begin(),u8.end()) + ": " + e.what());
    }
}
}
