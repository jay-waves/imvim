#include "editor.hpp"
#include <imgui_internal.h>
#include <chrono>
#include <iostream>
#include <fstream>
#include <iomanip>
#ifdef _WIN32
#include <shellapi.h>
#endif
int main(int argc, char** argv) {
    using namespace imvim;
    bool automated = false;
    try {
        std::vector<std::string> arguments;
#ifdef _WIN32
        int count = 0; LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!wide) throw std::runtime_error("Cannot read command line");
        for (int i = 1; i < count; ++i) {
            int n = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
            std::string value(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, value.data(), n, nullptr, nullptr);
            value.pop_back(); arguments.push_back(std::move(value));
        }
        LocalFree(wide); (void)argc; (void)argv;
#else
        for (int i = 1; i < argc; ++i) arguments.emplace_back(argv[i]);
#endif
        std::string nvim = "nvim", font, fallback, screenshot, stats;
        float font_size = 18; int smoke_seconds = 0;
        std::vector<std::string> nvim_args{"--embed"};
        bool pass = false;
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            const auto& arg = arguments[i];
            auto value = [&]() { if (++i >= arguments.size()) throw std::runtime_error("Missing value for " + arg); return arguments[i]; };
            if (pass) { nvim_args.push_back(arg); continue; }
            if (arg == "--") { pass = true; continue; }
            if (arg == "--nvim") nvim = value();
            else if (arg == "--font") font = value();
            else if (arg == "--fallback-font") fallback = value();
            else if (arg == "--font-size") font_size = std::stof(value());
            else if (arg == "--smoke-seconds") { automated = true; smoke_seconds = std::stoi(value()); }
            else if (arg == "--screenshot") screenshot = value();
            else if (arg == "--stats") stats = value();
            else if (arg == "--help") {
                const std::string help = "imvim [--nvim PATH] [--font FILE] [--fallback-font FILE] [--font-size 18]\n"
                    "      [--stats FILE] [--smoke-seconds N] [--screenshot FILE.ppm]\n"
                    "      [--clean] [-- Neovim arguments / files]\n"
                    "Ctrl+Shift+V: paste clipboard. Window close: Neovim save confirmation.\n"
                    "Smoke mode uses --clean and exits automatically. No terminal emulator is used.\n";
#ifdef _WIN32
                MessageBoxA(nullptr, help.c_str(), "imvim help", MB_OK);
#else
                std::cout << help;
#endif
                return 0;
            } else nvim_args.push_back(arg);
        }
        if (!(font_size >= 8 && font_size <= 72)) throw std::runtime_error("Font size must be 8..72");
        if (smoke_seconds < 0 || smoke_seconds > 300) throw std::runtime_error("Smoke duration must be 0..300 seconds");
        if (smoke_seconds) nvim_args = {"--embed", "--clean", "-n"};
        auto platform = make_platform();
        Session session(nvim, nvim_args, [&] { platform->wake(); });
        Editor editor(session, *platform, font, fallback, font_size);
        bool first = true, smoke_content = false;
        auto started = std::chrono::steady_clock::now();
        while (session.alive()) {
            // The pinned ImGui backend may defer input events between frames.
            bool queued_input = ImGui::GetCurrentContext()->InputEventsQueue.Size != 0;
            bool native = platform->events(!first && !queued_input);
            bool changed = editor.receive();
            if (!session.alive()) break;
            if (platform->take_close_request()) session.request("nvim_command", std::make_tuple(std::string("confirm qall")));
            if (first || native || changed || queued_input) { platform->begin_frame(); editor.frame(); platform->end_frame(); first = false; }
            if (smoke_seconds && editor.ready() && !smoke_content) {
                session.request("nvim_buf_set_lines", std::make_tuple(0, 0, -1, false, std::vector<std::string>{
                    "imvim / Neovim UI protocol + Dear ImGui", "", "A thin native frontend. No terminal emulator.",
                    "", "int main() {", "    return 0;", "}", "", "Unicode: 中文测试  λ → ✓", "", "Idle rendering sleeps until an event arrives."}));
                session.request("nvim_command", std::make_tuple(std::string("set number | setfiletype cpp")));
                smoke_content = true;
            }
            if (smoke_seconds && std::chrono::steady_clock::now() - started >= std::chrono::seconds(smoke_seconds)) {
                if (!editor.ready()) throw std::runtime_error("Neovim never produced a screen");
                if (!screenshot.empty()) platform->screenshot(screenshot);
                session.request("nvim_command", std::make_tuple(std::string("qa!")));
                break;
            }
        }
        if (!stats.empty()) {
            std::ofstream out(stats);
            out << "{\"frames\":" << editor.frames << ",\"seconds\":" << std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count() << ",\"grid_ready\":" << (editor.ready()?"true":"false") << ",\"renderer\":" << std::quoted(platform->renderer()) << "}\n";
            if (!out) throw std::runtime_error("Cannot write statistics");
        }
        if (!editor.ready()) throw std::runtime_error("Neovim exited before UI initialization");
        return 0;
    } catch (const std::exception& e) {
        if (automated) std::cerr << "imvim: " << e.what() << '\n';
        else show_error(e.what());
        return 1;
    }
}
