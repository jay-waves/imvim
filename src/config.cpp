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
        auto keys = [](const nlohmann::json& object, std::initializer_list<const char*> allowed, const char* section) {
            if (!object.is_object()) throw std::runtime_error(std::string(section) + " must be an object");
            for (const auto& entry : object.items()) {
                bool known = false;
                for (auto key : allowed) known |= entry.key() == key;
                if (!known) throw std::runtime_error(std::string("Unknown ") + section + " setting: " + entry.key());
            }
        };
        auto strings = [](const nlohmann::json& values) {
            if (!values.is_array()) throw std::runtime_error("Expected an array of strings");
            if (values.size() > 64) throw std::runtime_error("Too many array entries (maximum 64)");
            std::vector<std::string> result;
            for (const auto& value : values) result.push_back(value.get<std::string>());
            return result;
        };
        keys(json, {"font", "window", "nvim"}, "root");
        if (json.contains("font")) {
            const auto& font = json.at("font");
            keys(font, {"file", "family", "fallback_file", "fallback_families", "size", "line_spacing", "letter_spacing", "cjk_scale"}, "font");
            if (font.contains("file")) config.font = font_path(font.at("file"), path.parent_path());
            if (font.contains("family")) config.font_family = font.at("family").get<std::string>();
            if (!config.font.empty() && !config.font_family.empty()) throw std::runtime_error("Choose font.file or font.family, not both");
            if (font.contains("fallback_file")) config.fallback_font = font_path(font.at("fallback_file"), path.parent_path());
            if (font.contains("fallback_families")) {
                config.fallback_families = strings(font.at("fallback_families"));
                for (auto& name : config.fallback_families) if (name.empty()) throw std::runtime_error("Fallback family cannot be empty");
            }
            number(font, "size", config.font_size, 8, 72);
            number(font, "line_spacing", config.line_spacing, -2, 32);
            number(font, "letter_spacing", config.letter_spacing, -2, 16);
            number(font, "cjk_scale", config.cjk_scale, 0.5f, 2);
        }
        if (json.contains("window")) {
            const auto& window = json.at("window");
            keys(window, {"width", "height", "maximized"}, "window");
            auto dimension = [&](const char* key, int& destination, int low) {
                if (!window.contains(key)) return;
                if (!window.at(key).is_number_integer()) throw std::runtime_error(std::string(key) + " must be an integer");
                auto value = window.at(key).get<long long>();
                if (value < low || value > 8192) throw std::runtime_error(std::string(key) + " outside supported range");
                destination = static_cast<int>(value);
            };
            dimension("width", config.window_width, 320);
            dimension("height", config.window_height, 200);
            if (window.contains("maximized")) config.maximized = window.at("maximized").get<bool>();
        }
        if (json.contains("nvim")) {
            const auto& nvim = json.at("nvim");
            keys(nvim, {"path", "args"}, "nvim");
            if (nvim.contains("path")) {
                auto executable = nvim.at("path").get<std::string>();
                if (executable.empty()) throw std::runtime_error("nvim.path cannot be empty");
                // Bare executable names use PATH. Paths are relative to the configuration.
                config.nvim_path = executable.find_first_of("/\\") == std::string::npos ? executable : font_path(nvim.at("path"),path.parent_path());
            }
            if (nvim.contains("args")) config.nvim_args = strings(nvim.at("args"));
            for (const auto& arg : config.nvim_args)
                if (arg == "--headless" || arg == "--embed" || arg == "--listen" || arg == "--")
                    throw std::runtime_error("nvim.args contains a reserved UI transport option: " + arg);
        }
        return config;
    } catch (const std::exception& e) {
        auto u8 = path.u8string();
        throw std::runtime_error("Config " + std::string(u8.begin(),u8.end()) + ": " + e.what());
    }
}
}
