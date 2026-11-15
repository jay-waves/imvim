#include "editor.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
namespace imvim {
namespace {
ImU32 color(int rgb) { return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255); }
std::string utf8(unsigned c) {
    std::string s;
    if (c < 0x80) s += static_cast<char>(c);
    else if (c < 0x800) { s += static_cast<char>(0xc0 | (c >> 6)); s += static_cast<char>(0x80 | (c & 63)); }
    else if (c < 0x10000) { s += static_cast<char>(0xe0 | (c >> 12)); s += static_cast<char>(0x80 | ((c >> 6) & 63)); s += static_cast<char>(0x80 | (c & 63)); }
    else { s += static_cast<char>(0xf0 | (c >> 18)); s += static_cast<char>(0x80 | ((c >> 12) & 63)); s += static_cast<char>(0x80 | ((c >> 6) & 63)); s += static_cast<char>(0x80 | (c & 63)); }
    return s;
}
std::string default_font() {
#ifdef _WIN32
    const char* candidates[] = {"C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/cour.ttf"};
#else
    const char* candidates[] = {"/usr/share/fonts/adobe-source-code-pro-fonts/SourceCodePro-Regular.otf", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/liberation-mono-fonts/LiberationMono-Regular.ttf"};
#endif
    for (auto p : candidates) if (std::filesystem::exists(p)) return p;
    return {};
}
}
Editor::Editor(Session& session, Platform& platform, const std::string& path, const std::string& fallback, float size)
    : session_(session), platform_(platform), font_size_(size) {
    auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.LogFilename = nullptr;
    auto chosen = path.empty() ? default_font() : path;
    if (!chosen.empty()) {
        if (!std::filesystem::exists(std::filesystem::path(std::u8string(chosen.begin(), chosen.end())))) throw std::runtime_error("Font file not found: " + chosen);
        font_ = io.Fonts->AddFontFromFileTTF(chosen.c_str(), size);
    } else font_ = io.Fonts->AddFontDefault();
    if (!font_) throw std::runtime_error("Could not load font");
    auto merge = [&](const std::filesystem::path& file, bool icons = false) {
        if (!std::filesystem::exists(file))
            throw std::runtime_error("Missing fallback font. Keep the fonts directory beside imvim: " + file.string());
        auto bytes = file.u8string(); std::string filename(bytes.begin(), bytes.end());
        ImFontConfig config; config.MergeMode = true;
        if (icons) {
            // Symbols Only uses a square em. Fit it to the half-em terminal cell.
            config.ExtraSizeScale = 0.5f;
            config.GlyphOffset.y = -size * 0.125f;
        }
        if (!io.Fonts->AddFontFromFileTTF(filename.c_str(), size, &config))
            throw std::runtime_error("Could not load fallback font: " + filename);
    };
    // Resolve symbols before CJK: some CJK fonts contain unrelated private-use glyphs.
    merge(platform_.base_path() / "fonts" / "SymbolsNerdFontMono-Regular.ttf", true);
    merge(fallback.empty() ? platform_.base_path() / "fonts" / "DroidSansFallbackFull.ttf"
                           : std::filesystem::path(std::u8string(fallback.begin(), fallback.end())));
#ifdef _WIN32
    // Consolas and Droid do not cover all mathematical / UI symbols.
    // Use Windows' installed symbol font for the remaining glyphs.
    wchar_t windows_dir[MAX_PATH];
    UINT length = GetWindowsDirectoryW(windows_dir, MAX_PATH);
    if (length && length < MAX_PATH) {
        auto symbols = std::filesystem::path(windows_dir) / "Fonts" / "seguisym.ttf";
        if (std::filesystem::exists(symbols)) merge(symbols);
    }
#endif
    io.FontDefault = font_;
}
void Editor::check_glyphs() {
    auto* baked = font_->GetFontBaked(font_size_ * platform_.scale());
    for (unsigned codepoint : {0x4du, 0x4e2du, 0x6587u, 0x03bbu, 0x2192u, 0x2713u, 0xf07bu, 0xe0b0u, 0xf02d1u}) {
        if (!baked->FindGlyphNoFallback(codepoint))
            throw std::runtime_error("Font coverage check failed for Unicode codepoint " + std::to_string(codepoint));
    }
}
bool Editor::receive() {
    bool changed = false;
    for (auto& handle : session_.poll()) {
        const auto& o = handle.get();
        if (auto e = rpc_error(o); !e.empty()) throw std::runtime_error("Neovim RPC: " + e);
        if (o.type != msgpack::type::ARRAY || o.via.array.size != 3) continue;
        auto* a = o.via.array.ptr;
        if (a[0].as<int>() != 2 || a[1].as<std::string>() != "redraw") continue;
        const auto& events = a[2];
        if (events.type != msgpack::type::ARRAY) throw std::runtime_error("Invalid redraw payload");
        for (unsigned i = 0; i < events.via.array.size; ++i) {
            if (pending_.event(events.via.array.ptr[i])) {
                // Only publish completed frames; partial batches remain invisible.
                display_ = pending_; changed = true;
            }
        }
    }
    if (changed) platform_.title(display_.title.empty() ? "imvim" : display_.title + " — imvim");
    return changed;
}
void Editor::input() {
    auto& io = ImGui::GetIO();
    if (io.AppFocusLost) return;
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_V)) {
        if (const char* text = ImGui::GetClipboardText()) session_.paste(text);
    } else {
        struct Key { ImGuiKey code; const char* name; };
        static constexpr Key keys[] = {
            {ImGuiKey_Escape,"Esc"}, {ImGuiKey_Enter,"CR"}, {ImGuiKey_KeypadEnter,"CR"},
            {ImGuiKey_Tab,"Tab"}, {ImGuiKey_Backspace,"BS"}, {ImGuiKey_Delete,"Del"},
            {ImGuiKey_Insert,"Insert"}, {ImGuiKey_Home,"Home"}, {ImGuiKey_End,"End"},
            {ImGuiKey_PageUp,"PageUp"}, {ImGuiKey_PageDown,"PageDown"},
            {ImGuiKey_LeftArrow,"Left"}, {ImGuiKey_RightArrow,"Right"}, {ImGuiKey_UpArrow,"Up"}, {ImGuiKey_DownArrow,"Down"},
            {ImGuiKey_F1,"F1"}, {ImGuiKey_F2,"F2"}, {ImGuiKey_F3,"F3"}, {ImGuiKey_F4,"F4"},
            {ImGuiKey_F5,"F5"}, {ImGuiKey_F6,"F6"}, {ImGuiKey_F7,"F7"}, {ImGuiKey_F8,"F8"},
            {ImGuiKey_F9,"F9"}, {ImGuiKey_F10,"F10"}, {ImGuiKey_F11,"F11"}, {ImGuiKey_F12,"F12"}
        };
        std::string mods = std::string(io.KeyCtrl ? "C-" : "") + (io.KeyAlt ? "A-" : "") + (io.KeyShift ? "S-" : "");
        for (auto key : keys) if (ImGui::IsKeyPressed(key.code, true)) session_.input("<" + mods + key.name + ">");
        // Tick only while keys are held so ImGui can repeat navigation/control keys.
        if (io.KeyCtrl && !io.KeyAlt) {
            for (int k = ImGuiKey_A; k <= ImGuiKey_Z; ++k)
                if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), true))
                    session_.input("<" + mods + std::string(1, static_cast<char>('a' + k - ImGuiKey_A)) + ">");
            const Key ctrl[] = {{ImGuiKey_LeftBracket,"["}, {ImGuiKey_RightBracket,"]"}, {ImGuiKey_Backslash,"Bslash"}, {ImGuiKey_Space,"Space"}, {ImGuiKey_Minus,"-"}, {ImGuiKey_6,"6"}};
            for (auto key : ctrl) if (ImGui::IsKeyPressed(key.code, true)) session_.input("<" + mods + key.name + ">");
        }
        if (!io.KeyCtrl || io.KeyAlt)
            for (ImWchar c : io.InputQueueCharacters) if (c >= 32 && c != 127) session_.text(utf8(c));
    }
    int row = static_cast<int>(io.MousePos.y / cell_height_), col = static_cast<int>(io.MousePos.x / cell_width_);
    if (row < 0 || col < 0 || row >= display_.height || col >= display_.width) return;
    std::string mods = std::string(io.KeyCtrl ? "C" : "") + (io.KeyAlt ? "A" : "") + (io.KeyShift ? "S" : "");
    const char* buttons[] = {"left", "right", "middle"};
    for (int i = 0; i < 3; ++i) {
        const char* action = ImGui::IsMouseClicked(i) ? "press" : ImGui::IsMouseReleased(i) ? "release" :
            ImGui::IsMouseDown(i) && (io.MouseDelta.x || io.MouseDelta.y) ? "drag" : nullptr;
        if (action) session_.request("nvim_input_mouse", std::make_tuple(std::string(buttons[i]), std::string(action), mods, 0, row, col));
    }
    if (io.MouseWheel)
        for (int n = 0; n < std::min(10, static_cast<int>(std::ceil(std::abs(io.MouseWheel)))); ++n)
            session_.request("nvim_input_mouse", std::make_tuple(std::string("wheel"), std::string(io.MouseWheel > 0 ? "up" : "down"), mods, 0, row, col));
}
void Editor::draw() {
    auto* draw = ImGui::GetBackgroundDrawList();
    auto size = ImGui::GetIO().DisplaySize;
    draw->AddRectFilled({0, 0}, size, color(display_.background));
    for (int row = 0; row < display_.height; ++row) {
        for (int col = 0; col < display_.width;) {
            int begin = col;
            int background = display_.colors(display_.at(row, col).highlight).background;
            while (++col < display_.width && display_.colors(display_.at(row, col).highlight).background == background) {}
            draw->AddRectFilled({std::round(begin * cell_width_), row * cell_height_},
                                {std::round(col * cell_width_), (row + 1) * cell_height_}, color(background));
        }
        for (int col = 0; col < display_.width; ++col) {
            const auto& cell = display_.at(row, col);
            if (cell.text.empty()) continue; // continuation of a double-width glyph
            auto h = display_.colors(cell.highlight);
            ImVec2 p{std::round(col * cell_width_), row * cell_height_};
            int span = col + 1 < display_.width && display_.at(row, col + 1).text.empty() ? 2 : 1;
            float width = std::round((col + span) * cell_width_) - p.x;
            ImVec4 clip{p.x, p.y, p.x + width, p.y + cell_height_};
            draw->AddText(font_, font_size_ * platform_.scale(), p, color(h.foreground), cell.text.c_str(), nullptr, 0, &clip);
            if (h.underline || h.undercurl) draw->AddLine({p.x, p.y + cell_height_ - 2}, {p.x + width, p.y + cell_height_ - 2}, color(h.special));
            if (h.strike) draw->AddLine({p.x, p.y + cell_height_ / 2}, {p.x + width, p.y + cell_height_ / 2}, color(h.foreground));
        }
    }
    if (!display_.busy && display_.cursor_row >= 0 && display_.cursor_row < display_.height && display_.cursor_col >= 0 && display_.cursor_col < display_.width) {
        int row = display_.cursor_row, col = display_.cursor_col;
        ImVec2 p{std::round(col * cell_width_), row * cell_height_}, end{std::round((col + 1) * cell_width_), p.y + cell_height_};
        platform_.ime(p.x, p.y, cell_height_);
        float fraction = std::clamp(display_.cursor.percent / 100.0f, 0.05f, 1.0f);
        if (display_.cursor.shape == "vertical") end.x = p.x + std::max(2.0f, cell_width_ * fraction);
        if (display_.cursor.shape == "horizontal") p.y = end.y - std::max(2.0f, cell_height_ * fraction);
        auto h = display_.colors(display_.cursor.highlight);
        auto bg = display_.cursor.highlight ? h.background : display_.foreground;
        auto fg = display_.cursor.highlight ? h.foreground : display_.background;
        draw->AddRectFilled(p, end, color(bg));
        if (display_.cursor.shape == "block") {
            ImVec4 clip{p.x,p.y,end.x,end.y};
            draw->AddText(font_, font_size_ * platform_.scale(), p, color(fg), display_.at(row,col).text.c_str(), nullptr, 0, &clip);
        }
    }
}
void Editor::frame() {
    ++frames;
    float size = font_size_ * platform_.scale();
    ImGui::PushFont(font_, size);
    // CalcTextSize rounds its result to whole pixels. That is useful for widgets,
    // but adds space to every editor cell. Use the actual font advance instead.
    cell_width_ = ImGui::GetFontBaked()->GetCharAdvance('M');
    cell_height_ = std::ceil(size * 1.2f);
    auto available = ImGui::GetIO().DisplaySize;
    int columns = std::clamp(static_cast<int>(available.x / cell_width_), 2, 1000);
    int rows = std::clamp(static_cast<int>(available.y / cell_height_), 2, 1000);
    if (columns != columns_ || rows != rows_) {
        if (!columns_) session_.attach(columns, rows);
        else session_.request("nvim_ui_try_resize", std::make_tuple(columns, rows));
        columns_ = columns; rows_ = rows;
    }
    input(); draw();
    ImGui::PopFont();
}
}
