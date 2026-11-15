#include "platform.hpp"
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <fstream>
#include <vector>
#include <iostream>
#include <stdexcept>
namespace imvim {
namespace {
class SdlPlatform final : public Platform {
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    Uint32 wake_event_ = 0;
    bool close_ = false;
public:
    SdlPlatform() {
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
        window_ = SDL_CreateWindow("imvim", 1000, 700, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window_) { SDL_Quit(); throw std::runtime_error(SDL_GetError()); }
        context_ = SDL_GL_CreateContext(window_);
        if (!context_) { SDL_DestroyWindow(window_); SDL_Quit(); throw std::runtime_error(SDL_GetError()); }
        SDL_GL_MakeCurrent(window_, context_); SDL_GL_SetSwapInterval(1);
        ImGui::CreateContext();
        ImGui_ImplSDL3_InitForOpenGL(window_, context_); ImGui_ImplOpenGL3_Init("#version 330 core");
        wake_event_ = SDL_RegisterEvents(1);
        SDL_StartTextInput(window_);
    }
    ~SdlPlatform() override {
        ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext();
        SDL_GL_DestroyContext(context_); SDL_DestroyWindow(window_); SDL_Quit();
    }
    bool events(bool wait) override {
        bool held = false;
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
            held |= ImGui::IsKeyDown(static_cast<ImGuiKey>(k));
        bool dirty = held;
        SDL_Event event;
        auto handle = [&](const SDL_Event& e) {
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) close_ = true;
            if (e.type != wake_event_) { ImGui_ImplSDL3_ProcessEvent(&e); dirty = true; }
        };
        if (wait && SDL_WaitEventTimeout(&event, held ? 16 : 1000)) handle(event);
        while (SDL_PollEvent(&event)) handle(event);
        return dirty;
    }
    bool take_close_request() override { bool value = close_; close_ = false; return value; }
    void wake() override { SDL_Event e{}; e.type = wake_event_; SDL_PushEvent(&e); }
    void begin_frame() override { ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame(); }
    void end_frame() override {
        ImGui::Render();
        int w, h; SDL_GetWindowSizeInPixels(window_, &w, &h);
        glViewport(0, 0, w, h); glClearColor(0.12f, 0.13f, 0.16f, 1); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); SDL_GL_SwapWindow(window_);
    }
    void title(const std::string& title) override { SDL_SetWindowTitle(window_, title.c_str()); }
    void ime(float x, float y, float height) override {
        SDL_Rect area{static_cast<int>(x), static_cast<int>(y), 1, static_cast<int>(height)};
        SDL_SetTextInputArea(window_, &area, 0);
    }
    float scale() const override { return 1.0f; } // SDL/ImGui use logical coordinates; framebuffer scale handles pixels.
    std::string renderer() const override {
        const auto* name = glGetString(GL_RENDERER);
        return name ? reinterpret_cast<const char*>(name) : "OpenGL (unknown renderer)";
    }
    void screenshot(const std::string& path) override {
        int w, h; SDL_GetWindowSizeInPixels(window_, &w, &h);
        std::vector<unsigned char> pixels(static_cast<std::size_t>(w) * h * 3);
        // Some X11 drivers discard the front buffer after Present. Re-render
        // the last completed draw list to the back buffer before readback.
        glViewport(0, 0, w, h); glClearColor(0.12f, 0.13f, 0.16f, 1); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadBuffer(GL_BACK); glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        std::ofstream out(path, std::ios::binary); out << "P6\n" << w << ' ' << h << "\n255\n";
        for (int y = h - 1; y >= 0; --y) out.write(reinterpret_cast<char*>(pixels.data() + y * w * 3), w * 3);
        if (!out) throw std::runtime_error("Cannot write screenshot");
    }
};
}
std::unique_ptr<Platform> make_platform() { return std::make_unique<SdlPlatform>(); }
void show_error(const std::string& message) { std::cerr << "imvim: " << message << '\n'; }
}
