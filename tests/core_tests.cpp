#include "grid.hpp"
#include "config.hpp"
#include "font_lookup.hpp"
#include "session.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <map>
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class T> msgpack::object_handle object(const T& value) {
    msgpack::sbuffer buffer; msgpack::pack(buffer, value); return msgpack::unpack(buffer.data(), buffer.size());
}
template<class T> bool event(imvim::Grid& grid, const T& value) { auto o = object(value); return grid.event(o.get()); }
void config_test() {
    auto directory = std::filesystem::temp_directory_path() / ("imvim-config-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(p, ec); } } cleanup{directory};
    auto path = directory / "config.json";
    require(imvim::load_config(path).font_size == 16, "Missing config uses defaults");
    auto write = [&](const std::string& contents) { std::ofstream file(path); file << contents; };
    write(R"({"font":{"file":"fonts/mono.ttf","size":14.5,"line_spacing":2,"letter_spacing":-0.5}})");
    auto config = imvim::load_config(path);
    require(config.font_size == 14.5f && config.line_spacing == 2 && config.letter_spacing == -0.5f, "Numeric config settings");
    auto expected = (directory / "fonts" / "mono.ttf").u8string();
    require(config.font == std::string(expected.begin(),expected.end()), "Font paths resolve relative to config");
    write(R"({"font":{"family":"Consolas","fallback_families":["Segoe UI Symbol"],"cjk_scale":1.15},"window":{"width":1280,"height":800,"maximized":true},"nvim":{"path":"nvim","args":["--clean","-n"]}})");
    config = imvim::load_config(path);
    require(config.font_family == "Consolas" && config.fallback_families.size() == 1 && config.cjk_scale > 1.14f, "System family and CJK scale");
    require(config.window_width == 1280 && config.window_height == 800 && config.maximized, "Window settings");
    require(config.nvim_path == "nvim" && config.nvim_args == std::vector<std::string>({"--clean","-n"}), "Neovim settings");
#ifdef _WIN32
    auto face = imvim::find_system_font("Consolas");
#else
    auto face = imvim::find_system_font("monospace");
#endif
    require(std::filesystem::exists(face.file) && face.index >= 0, "Resolve installed system font");
    for (const auto* invalid : {"{", "[]", R"({"font":{"size":0}})", R"({"font":{"size":"16"}})", R"({"font":{"szie":16}})", R"({"font":{"file":12}})", R"({"font":{"family":"Consolas","file":"font.ttf"}})", R"({"font":{"cjk_scale":0}})", R"({"window":{"width":1}})", R"({"window":{"height":500.5}})", R"({"window":{"maximized":1}})", R"({"nvim":{"args":"--clean"}})", R"({"nvim":{"args":["--headless"]}})", R"({"nvim":{"path":""}})"}) {
        write(invalid); bool rejected = false;
        try { imvim::load_config(path); } catch (const std::exception& e) { rejected = std::string(e.what()).find("config.json") != std::string::npos; }
        require(rejected, "Invalid config includes file path in error");
    }
}
void unit() {
    config_test();
    imvim::Grid g;
    event(g, std::make_tuple("grid_resize", std::make_tuple(1, 5, 3)));
    event(g, std::make_tuple("grid_line", std::make_tuple(1, 0, 0, std::vector<std::tuple<std::string,int,int>>{{"A",2,2},{"中",3,1},{"",3,1},{"Z",4,1}})));
    require(g.at(0,0).text == "A" && g.at(0,1).highlight == 2, "Repeated grid cells");
    require(g.at(0,2).text == "中" && g.at(0,3).text.empty(), "Wide character continuation");
    event(g, std::make_tuple("grid_scroll", std::make_tuple(1,0,3,0,5,-1,0)));
    require(g.at(1,2).text == "中" && g.at(0,0).text == " ", "Scroll down overlap");
    event(g, std::make_tuple("grid_scroll", std::make_tuple(1,0,3,0,5,1,0)));
    require(g.at(0,4).text == "Z" && g.at(1,0).text == " ", "Scroll up overlap");
    event(g, std::make_tuple("grid_scroll", std::make_tuple(1,0,1,0,5,0,1)));
    require(g.at(0,1).text == "中" && g.at(0,4).text == " ", "Horizontal scroll");
    event(g, std::make_tuple("default_colors_set", std::make_tuple(0x112233,0x445566,0x778899,0,0)));
    g.highlights[7].reverse = true;
    require(g.colors(7).foreground == 0x445566 && g.colors(7).background == 0x112233, "Default reverse colors");
    require(event(g, std::make_tuple("flush", std::tuple<>{})), "Flush boundary");
    require(imvim::Session::escape_text("a<b中") == "a<LT>b中", "Literal input escaping");
    bool rejected = false;
    try { event(g, std::make_tuple("grid_resize", std::make_tuple(1,100000,100000))); } catch (...) { rejected = true; }
    require(rejected, "Oversize grid rejected");
    // Every split point must decode the same RPC frame, including a split UTF-8 string.
    auto payload = std::make_tuple(2, std::string("redraw"), std::vector<std::string>{"中文", "hello"});
    msgpack::sbuffer encoded; msgpack::pack(encoded, payload);
    for (std::size_t split = 1; split < encoded.size(); ++split) {
        msgpack::unpacker parser; msgpack::object_handle decoded;
        parser.reserve_buffer(split); std::memcpy(parser.buffer(), encoded.data(), split); parser.buffer_consumed(split);
        require(!parser.next(decoded), "Incomplete frame must wait");
        auto remaining = encoded.size()-split; parser.reserve_buffer(remaining);
        std::memcpy(parser.buffer(), encoded.data()+split, remaining); parser.buffer_consumed(remaining);
        require(parser.next(decoded) && decoded.get().as<decltype(payload)>() == payload, "Fragmented RPC roundtrip");
    }
}
void integration() {
    imvim::Session session("nvim", {"--embed", "--clean", "-n"});
    imvim::Grid grid;
    bool flushed = false;
    auto pump = [&](const auto& done) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while (std::chrono::steady_clock::now() < deadline) {
            for (auto& h : session.poll()) {
                auto& o = h.get();
                if (auto e = imvim::rpc_error(o); !e.empty()) throw std::runtime_error(e);
                if (o.type == msgpack::type::ARRAY && o.via.array.size == 3) {
                    auto* a = o.via.array.ptr;
                    if (a[0].as<int>() == 2 && a[1].as<std::string>() == "redraw")
                        for (unsigned i=0; i<a[2].via.array.size; ++i) flushed |= grid.event(a[2].via.array.ptr[i]);
                }
            }
            if (done()) return;
            if (!session.alive()) throw std::runtime_error("Neovim exited unexpectedly");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        throw std::runtime_error("Timed out waiting for Neovim");
    };
    session.attach(60,20);
    pump([&] { return flushed && grid.width == 60; });
    session.input("iHello "); session.text("中文 <literal>"); session.input("<Esc>");
    pump([&] {
        std::string line; for (int x=0; x<grid.width; ++x) line += grid.at(0,x).text;
        return line.find("Hello 中文 <literal>") != std::string::npos && grid.mode == "normal";
    });
    auto name = "imvim test " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + " 中文.txt";
    auto path = std::filesystem::temp_directory_path() / std::filesystem::path(std::u8string(name.begin(), name.end()));
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code e; std::filesystem::remove(p,e); } } cleanup{path};
    auto u8 = path.u8string(); std::string filename(u8.begin(),u8.end());
    session.request("nvim_exec_lua", std::make_tuple(std::string("vim.api.nvim_buf_set_name(0, ...); vim.cmd('write')"), std::vector<std::string>{filename}));
    pump([&] { return std::filesystem::exists(path) && std::filesystem::file_size(path)>0; });
    std::ifstream file(path, std::ios::binary); std::string line; std::getline(file,line);
    if (!line.empty() && line.back()=='\r') line.pop_back();
    require(line == "Hello 中文 <literal>", "Unicode edit and disk save");
    session.request("nvim_ui_try_resize", std::make_tuple(72,24));
    pump([&] { return grid.width == 72 && grid.height == 24; });
    session.request("nvim_command", std::make_tuple(std::string("qa!")));
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (session.alive() && std::chrono::steady_clock::now()<deadline) { session.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    require(!session.alive(), "Neovim exits cleanly");
    bool failed = false;
    try { imvim::Session invalid("imvim-nonexistent-nvim-executable", {}); } catch (...) { failed = true; }
    require(failed, "Missing executable reported");
}
}
int main(int argc, char**) {
    try { if (argc > 1) integration(); else unit(); std::cout << "PASS\n"; return 0; }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
