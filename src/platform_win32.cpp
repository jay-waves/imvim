#include "platform.hpp"
#include <windows.h>
#include <imm.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <stdexcept>
#include <atomic>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
namespace imvim {
namespace {
using Microsoft::WRL::ComPtr;
std::wstring wide(const std::string& text) {
    int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(n, 0); MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n); return out;
}
void check(HRESULT result, const char* operation) { if (FAILED(result)) throw std::runtime_error(operation); }
class WinPlatform final : public Platform {
    HWND window_ = nullptr;
    HANDLE wake_ = nullptr;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain> swap_;
    ComPtr<ID3D11RenderTargetView> target_;
    unsigned resize_w_ = 0, resize_h_ = 0;
    bool close_ = false, dirty_ = true;
    float scale_ = 1;
    static LRESULT CALLBACK procedure(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<WinPlatform*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            self = static_cast<WinPlatform*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self) {
            if (msg == WM_CLOSE) { self->close_ = true; return 0; }
            if (msg == WM_SIZE && wp != SIZE_MINIMIZED) { self->resize_w_ = LOWORD(lp); self->resize_h_ = HIWORD(lp); }
            if (msg == WM_DPICHANGED) {
                self->scale_ = HIWORD(wp) / 96.0f;
                auto* r = reinterpret_cast<RECT*>(lp);
                SetWindowPos(hwnd, nullptr, r->left, r->top, r->right-r->left, r->bottom-r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            if (msg == WM_PAINT) { PAINTSTRUCT ps; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps); self->dirty_ = true; return 0; }
            if (msg == WM_ERASEBKGND) return 1;
        }
        if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;
        if (msg == WM_SYSCOMMAND && (wp & 0xfff0) == SC_KEYMENU) return 0;
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    void target() {
        ComPtr<ID3D11Texture2D> buffer;
        check(swap_->GetBuffer(0, IID_PPV_ARGS(&buffer)), "DX11 GetBuffer failed");
        check(device_->CreateRenderTargetView(buffer.Get(), nullptr, &target_), "DX11 target creation failed");
    }
public:
    WinPlatform() {
        ImGui_ImplWin32_EnableDpiAwareness();
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = procedure;
        wc.hInstance = GetModuleHandleW(nullptr); wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM); wc.lpszClassName = L"imvim.window";
        if (!RegisterClassExW(&wc)) throw std::runtime_error("RegisterClass failed");
        window_ = CreateWindowExW(0, wc.lpszClassName, L"imvim", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700, nullptr, nullptr, wc.hInstance, this);
        if (!window_) throw std::runtime_error("CreateWindow failed");
        scale_ = ImGui_ImplWin32_GetDpiScaleForHwnd(window_);
        wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!wake_) throw std::runtime_error("CreateEvent failed");
        DXGI_SWAP_CHAIN_DESC desc{}; desc.BufferCount = 2; desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.OutputWindow = window_;
        desc.SampleDesc.Count = 1; desc.Windowed = TRUE; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        auto create = [&](D3D_DRIVER_TYPE type) { return D3D11CreateDeviceAndSwapChain(nullptr, type, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &desc, &swap_, &device_, nullptr, &context_); };
        HRESULT result = create(D3D_DRIVER_TYPE_HARDWARE);
        if (FAILED(result)) result = create(D3D_DRIVER_TYPE_WARP);
        check(result, "Cannot initialize Direct3D 11"); target();
        ImGui::CreateContext(); ImGui_ImplWin32_Init(window_); ImGui_ImplDX11_Init(device_.Get(), context_.Get());
        ShowWindow(window_, SW_SHOWDEFAULT); UpdateWindow(window_);
    }
    ~WinPlatform() override {
        ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
        if (wake_) CloseHandle(wake_);
        DestroyWindow(window_); UnregisterClassW(L"imvim.window", GetModuleHandleW(nullptr));
    }
    bool events(bool wait) override {
        bool held = false;
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) held |= ImGui::IsKeyDown(static_cast<ImGuiKey>(k));
        if (wait) MsgWaitForMultipleObjectsEx(1, &wake_, held ? 16 : 1000, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        bool dirty = dirty_ || held; dirty_ = false;
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageW(&msg); dirty = true;
        }
        return dirty;
    }
    bool take_close_request() override { bool value = close_; close_ = false; return value; }
    void wake() override { SetEvent(wake_); }
    void begin_frame() override {
        if (resize_w_ && resize_h_) {
            context_->OMSetRenderTargets(0, nullptr, nullptr); target_.Reset();
            check(swap_->ResizeBuffers(0, resize_w_, resize_h_, DXGI_FORMAT_UNKNOWN, 0), "DX11 resize failed");
            target(); resize_w_ = resize_h_ = 0;
        }
        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
    }
    void end_frame() override {
        ImGui::Render(); auto* view = target_.Get(); context_->OMSetRenderTargets(1, &view, nullptr);
        const float clear[] = {0.12f,0.13f,0.16f,1}; context_->ClearRenderTargetView(view, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        check(swap_->Present(1, 0), "DX11 present failed");
    }
    void title(const std::string& text) override { SetWindowTextW(window_, wide(text).c_str()); }
    void ime(float x, float y, float height) override {
        HIMC imc = ImmGetContext(window_);
        if (!imc) return;
        COMPOSITIONFORM composition{}; composition.dwStyle = CFS_POINT; composition.ptCurrentPos = {static_cast<LONG>(x), static_cast<LONG>(y + height)};
        ImmSetCompositionWindow(imc, &composition);
        CANDIDATEFORM candidate{}; candidate.dwStyle = CFS_CANDIDATEPOS; candidate.ptCurrentPos = composition.ptCurrentPos;
        ImmSetCandidateWindow(imc, &candidate); ImmReleaseContext(window_, imc);
    }
    float scale() const override { return scale_; }
    std::string renderer() const override {
        ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter; DXGI_ADAPTER_DESC desc{};
        if (FAILED(device_.As(&dxgi)) || FAILED(dxgi->GetAdapter(&adapter)) || FAILED(adapter->GetDesc(&desc))) return "Direct3D 11";
        int n = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nullptr, 0, nullptr, nullptr);
        std::string text(n, 0); WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, text.data(), n, nullptr, nullptr);
        if (!text.empty()) text.pop_back();
        return "Direct3D 11 / " + text;
    }
    void screenshot(const std::string&) override { throw std::runtime_error("--screenshot is available only on the Linux validation backend"); }
};
}
std::unique_ptr<Platform> make_platform() { return std::make_unique<WinPlatform>(); }
void show_error(const std::string& text) { MessageBoxW(nullptr, wide(text).c_str(), L"imvim", MB_OK | MB_ICONERROR); }
}
