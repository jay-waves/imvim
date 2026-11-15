#pragma once
#include <msgpack.hpp>
#include <string>
#include <unordered_map>
#include <vector>
namespace imvim {
struct Cell { std::string text = " "; int highlight = 0; };
struct Highlight {
    int foreground = -1, background = -1, special = -1;
    bool reverse = false, bold = false, italic = false, underline = false, undercurl = false, strike = false;
};
struct CursorStyle { std::string shape = "block"; int percent = 100; int highlight = 0; };
struct Grid {
    int width = 0, height = 0, cursor_row = 0, cursor_col = 0;
    int foreground = 0xd8dee9, background = 0x20242c, special = 0xd8dee9;
    std::vector<Cell> cells;
    std::unordered_map<int, Highlight> highlights;
    std::vector<CursorStyle> modes;
    CursorStyle cursor;
    std::string title = "imvim", mode = "normal";
    bool busy = false;
    // Returns true only for a completed flush. Unknown optional events are ignored.
    bool event(const msgpack::object& event);
    const Cell& at(int row, int col) const { return cells.at(static_cast<std::size_t>(row * width + col)); }
    Highlight colors(int id) const;
};
std::string rpc_error(const msgpack::object& object);
}
