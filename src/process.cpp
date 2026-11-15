#include "process.hpp"
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <thread>
#ifdef _WIN32
namespace {
std::wstring wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n && !s.empty()) throw std::runtime_error("Invalid UTF-8 argument");
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}
std::wstring quote(const std::string& s) {
    std::wstring out = L"\"";
    unsigned slashes = 0;
    for (wchar_t c : wide(s)) {
        if (c == L'\\') { ++slashes; continue; }
        out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L'"';
}
}
#else
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#endif
namespace imvim {
Process::Process(const std::string& executable, const std::vector<std::string>& args) {
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE child_in = nullptr, child_out = nullptr;
    if (!CreatePipe(&child_in, &input_, &sa, 0)) throw std::runtime_error("CreatePipe stdin failed");
    if (!CreatePipe(&output_, &child_out, &sa, 0)) {
        CloseHandle(child_in); CloseHandle(input_); input_ = nullptr;
        throw std::runtime_error("CreatePipe stdout failed");
    }
    SetHandleInformation(input_, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(output_, HANDLE_FLAG_INHERIT, 0);
    HANDLE err = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = child_in; si.hStdOutput = child_out; si.hStdError = err;
    PROCESS_INFORMATION pi{};
    std::wstring command = quote(executable);
    for (const auto& a : args) command += L" " + quote(a);
    BOOL ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    DWORD error = GetLastError();
    CloseHandle(child_in); CloseHandle(child_out);
    if (err != INVALID_HANDLE_VALUE) CloseHandle(err);
    if (!ok) {
        CloseHandle(input_); CloseHandle(output_); input_ = output_ = nullptr;
        throw std::runtime_error("Cannot start Neovim (Windows error " + std::to_string(error) + "). Install nvim on PATH or use --nvim PATH.");
    }
    CloseHandle(pi.hThread); child_ = pi.hProcess;
#else
    int in[2], out[2], status[2];
    if (pipe(in) < 0) throw std::runtime_error("pipe stdin failed");
    if (pipe(out) < 0) { close(in[0]); close(in[1]); throw std::runtime_error("pipe stdout failed"); }
    if (pipe(status) < 0) { for (int fd : {in[0], in[1], out[0], out[1]}) close(fd); throw std::runtime_error("pipe status failed"); }
    fcntl(status[1], F_SETFD, FD_CLOEXEC);
    std::vector<char*> argv{const_cast<char*>(executable.c_str())};
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    child_ = fork();
    if (child_ == 0) {
        close(status[0]); dup2(in[0], STDIN_FILENO); dup2(out[1], STDOUT_FILENO);
        for (int fd : {in[0], in[1], out[0], out[1]}) close(fd);
        execvp(executable.c_str(), argv.data());
        int e = errno; (void)::write(status[1], &e, sizeof(e)); _exit(127);
    }
    close(in[0]); close(out[1]); close(status[1]);
    if (child_ < 0) { close(in[1]); close(out[0]); close(status[0]); throw std::runtime_error("fork failed"); }
    input_ = in[1]; output_ = out[0];
    int e = 0; ssize_t count;
    do { count = ::read(status[0], &e, sizeof(e)); } while (count < 0 && errno == EINTR);
    close(status[0]);
    if (count > 0) { stop(); close(output_); output_ = -1; throw std::runtime_error("Cannot start Neovim: " + std::string(strerror(e))); }
    signal(SIGPIPE, SIG_IGN);
#endif
}
Process::~Process() {
    stop();
#ifdef _WIN32
    if (output_) CloseHandle(output_);
#else
    if (output_ >= 0) close(output_);
#endif
}
std::size_t Process::read(char* data, std::size_t size) {
#ifdef _WIN32
    DWORD n = 0;
    if (!ReadFile(output_, data, static_cast<DWORD>(size), &n, nullptr)) return 0;
    return n;
#else
    ssize_t n;
    do { n = ::read(output_, data, size); } while (n < 0 && errno == EINTR);
    if (n < 0) throw std::runtime_error("Read from Neovim failed");
    return static_cast<std::size_t>(n);
#endif
}
void Process::write(const char* data, std::size_t size) {
    while (size) {
#ifdef _WIN32
        DWORD n = 0;
        if (!WriteFile(input_, data, static_cast<DWORD>(std::min<std::size_t>(size, 65536)), &n, nullptr) || !n)
            throw std::runtime_error("Neovim pipe closed");
#else
        auto n = ::write(input_, data, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error("Neovim pipe closed");
#endif
        data += n; size -= n;
    }
}
void Process::stop() {
#ifdef _WIN32
    if (input_) { CloseHandle(input_); input_ = nullptr; }
    if (child_) {
        if (WaitForSingleObject(child_, 500) == WAIT_TIMEOUT) {
            TerminateProcess(child_, 1); WaitForSingleObject(child_, INFINITE);
        }
        CloseHandle(child_); child_ = nullptr;
    }
#else
    if (input_ >= 0) { close(input_); input_ = -1; }
    if (child_ > 0) {
        for (int i = 0; i < 50; ++i) {
            auto r = waitpid(child_, nullptr, WNOHANG);
            if (r == child_ || (r < 0 && errno == ECHILD)) { child_ = -1; return; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        kill(child_, SIGKILL);
        while (waitpid(child_, nullptr, 0) < 0 && errno == EINTR) {}
        child_ = -1;
    }
#endif
}
}
