#pragma once
#include "grid.hpp"
#include "session.hpp"
#include "platform.hpp"
#include <imgui.h>
namespace imvim {
class Editor {
public:
    Editor(Session& session, Platform& platform, const Config& config);
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
    float line_spacing_ = 0, letter_spacing_ = 0, cjk_scale_ = 1;
    int columns_ = 0, rows_ = 0;
    void input();
    void draw();
    void draw_text(int row, int col, ImU32 foreground);
};
}
