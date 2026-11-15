#pragma once
#include <memory>
#include <string>
namespace imvim {
class Platform {
public:
    virtual ~Platform() = default;
    // Wait for native input or RPC wakeup; never continuously render while idle.
    virtual bool events(bool wait) = 0;
    virtual bool take_close_request() = 0;
    virtual void wake() = 0;
    virtual void begin_frame() = 0;
    virtual void end_frame() = 0;
    virtual void title(const std::string& value) = 0;
    virtual void ime(float x, float y, float height) = 0;
    virtual float scale() const = 0;
    virtual std::string renderer() const = 0;
    virtual void screenshot(const std::string& path) = 0;
};
std::unique_ptr<Platform> make_platform();
void show_error(const std::string& message);
}
