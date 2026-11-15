#include "grid.hpp"
#include <algorithm>
#include <sstream>
#include <stdexcept>
namespace imvim {
namespace {
const msgpack::object& item(const msgpack::object& o, unsigned i) {
    if (o.type != msgpack::type::ARRAY || i >= o.via.array.size) throw std::runtime_error("Malformed Neovim grid event");
    return o.via.array.ptr[i];
}
int number(const msgpack::object& o, unsigned i) { return item(o, i).as<int>(); }
void properties(const msgpack::object& o, const auto& f) {
    if (o.type != msgpack::type::MAP) return;
    for (unsigned i = 0; i < o.via.map.size; ++i) f(o.via.map.ptr[i].key.as<std::string>(), o.via.map.ptr[i].val);
}
}
Highlight Grid::colors(int id) const {
    Highlight h;
    if (auto it = highlights.find(id); it != highlights.end()) h = it->second;
    if (h.foreground < 0) h.foreground = foreground;
    if (h.background < 0) h.background = background;
    if (h.special < 0) h.special = special;
    if (h.reverse) std::swap(h.foreground, h.background);
    return h;
}
bool Grid::event(const msgpack::object& event) {
    auto name = item(event, 0).as<std::string>();
    if (name == "flush") return true;
    if (name == "busy_start") busy = true;
    if (name == "busy_stop") busy = false;
    for (unsigned i = 1; i < event.via.array.size; ++i) {
        const auto& a = item(event, i);
        if (name == "grid_resize" && number(a, 0) == 1) {
            int w = number(a, 1), h = number(a, 2);
            if (w < 1 || h < 1 || w > 4096 || h > 4096 || static_cast<long long>(w) * h > 1000000)
                throw std::runtime_error("Invalid or oversized Neovim grid");
            width = w; height = h; cells.assign(static_cast<std::size_t>(w * h), {});
        } else if (name == "grid_clear" && number(a, 0) == 1) {
            std::fill(cells.begin(), cells.end(), Cell{});
        } else if (name == "grid_cursor_goto" && number(a, 0) == 1) {
            cursor_row = number(a, 1); cursor_col = number(a, 2);
        } else if (name == "grid_line" && number(a, 0) == 1) {
            int row = number(a, 1), col = number(a, 2), hl = 0;
            const auto& run = item(a, 3);
            if (row < 0 || row >= height || run.type != msgpack::type::ARRAY) throw std::runtime_error("Invalid grid line");
            for (unsigned j = 0; j < run.via.array.size; ++j) {
                const auto& c = item(run, j);
                auto text = item(c, 0).as<std::string>();
                if (c.via.array.size > 1) hl = number(c, 1);
                int repeat = c.via.array.size > 2 ? number(c, 2) : 1;
                if (repeat < 0 || col < 0 || col + repeat > width) throw std::runtime_error("Grid line outside bounds");
                for (int n = 0; n < repeat; ++n) cells[row * width + col++] = {text, hl};
            }
        } else if (name == "grid_scroll" && number(a, 0) == 1) {
            int top = number(a, 1), bottom = number(a, 2), left = number(a, 3), right = number(a, 4);
            int rows = number(a, 5), cols = number(a, 6);
            if (top < 0 || bottom > height || left < 0 || right > width || top > bottom || left > right)
                throw std::runtime_error("Invalid scroll bounds");
            // Directional in-place copy avoids an extra screen allocation.
            for (int y = rows >= 0 ? top : bottom - 1; y >= top && y < bottom; y += rows >= 0 ? 1 : -1)
                for (int x = cols >= 0 ? left : right - 1; x >= left && x < right; x += cols >= 0 ? 1 : -1) {
                    int sy = y + rows, sx = x + cols;
                    cells[y * width + x] = sy >= top && sy < bottom && sx >= left && sx < right ? cells[sy * width + sx] : Cell{};
                }
        } else if (name == "default_colors_set") {
            foreground = number(a, 0); background = number(a, 1); special = number(a, 2);
            if (foreground < 0) foreground = 0xd8dee9;
            if (background < 0) background = 0x20242c;
            if (special < 0) special = foreground;
        } else if (name == "hl_attr_define") {
            Highlight h;
            properties(item(a, 1), [&](const std::string& k, const msgpack::object& v) {
                if (k == "foreground") h.foreground = v.as<int>();
                else if (k == "background") h.background = v.as<int>();
                else if (k == "special") h.special = v.as<int>();
                else if (k == "reverse") h.reverse = v.as<bool>();
                else if (k == "bold") h.bold = v.as<bool>();
                else if (k == "italic") h.italic = v.as<bool>();
                else if (k == "underline") h.underline = v.as<bool>();
                else if (k == "undercurl") h.undercurl = v.as<bool>();
                else if (k == "strikethrough") h.strike = v.as<bool>();
            });
            if (highlights.size() > 65536) throw std::runtime_error("Highlight table exceeded limit");
            highlights[number(a, 0)] = h;
        } else if (name == "mode_info_set") {
            modes.clear();
            const auto& list = item(a, 1);
            if (list.type != msgpack::type::ARRAY) continue;
            for (unsigned j = 0; j < list.via.array.size; ++j) {
                CursorStyle c;
                properties(item(list, j), [&](const std::string& k, const msgpack::object& v) {
                    if (k == "cursor_shape") c.shape = v.as<std::string>();
                    else if (k == "cell_percentage") c.percent = v.as<int>();
                    else if (k == "attr_id") c.highlight = v.as<int>();
                });
                modes.push_back(c);
            }
        } else if (name == "mode_change") {
            mode = item(a, 0).as<std::string>(); int index = number(a, 1);
            if (index >= 0 && static_cast<std::size_t>(index) < modes.size()) cursor = modes[index];
        } else if (name == "set_title") title = item(a, 0).as<std::string>();
    }
    return false;
}
std::string rpc_error(const msgpack::object& o) {
    if (o.type != msgpack::type::ARRAY || o.via.array.size != 4) return {};
    auto* a = o.via.array.ptr;
    if (a[0].as<int>() != 1 || a[2].is_nil()) return {};
    std::ostringstream stream; stream << a[2]; return stream.str();
}
}
