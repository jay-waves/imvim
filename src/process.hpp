#pragma once
#include <cstddef>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
namespace imvim {
class Process {
public:
    Process(const std::string& executable, const std::vector<std::string>& args);
    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    std::size_t read(char* data, std::size_t size);
    void write(const char* data, std::size_t size);
    void stop();
private:
#ifdef _WIN32
    HANDLE input_ = nullptr, output_ = nullptr, child_ = nullptr;
#else
    int input_ = -1, output_ = -1, child_ = -1;
#endif
};
}
