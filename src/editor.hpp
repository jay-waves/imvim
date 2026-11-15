#pragma once
#include "grid.hpp"
#include "session.hpp"
#include "platform.hpp"
#include <imgui.h>
namespace imvim {
class Editor {
public:
    Editor(Session& session, Platform& platform, const std::string& font, const std::string& fallback, float size);
    bool receive();
    void frame();
    std::size_t frames = 0;
    void check_glyphs();
    bool ready() const { return display_.width != 0; }
private:
    Session& session_;
    Platform& platform_;
    Grid pending_, display_;
    ImFont* font_ = nullptr;
    float font_size_ = 16, cell_width_ = 10, cell_height_ = 22;
    int columns_ = 0, rows_ = 0;
    void input();
    void draw();
};
}
