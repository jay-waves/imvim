#pragma once
#include "process.hpp"
#include <msgpack.hpp>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
namespace imvim {
class Session {
public:
    Session(const std::string& executable, const std::vector<std::string>& args, std::function<void()> wake = {});
    ~Session();
    Session(const Session&) = delete;
    bool alive() const { return alive_; }
    std::vector<msgpack::object_handle> poll();
    template<class Args> unsigned request(const std::string& method, const Args& args) {
        unsigned id = ++next_id_;
        msgpack::sbuffer buffer;
        msgpack::pack(buffer, std::make_tuple(0, id, method, args));
        process_.write(buffer.data(), buffer.size());
        return id;
    }
    void attach(int columns, int rows);
    void input(std::string text);
    void text(const std::string& text);
    void paste(const std::string& text);
    static std::string escape_text(const std::string& text);
private:
    void read_loop();
    void send_input();
    Process process_;
    std::function<void()> wake_;
    std::atomic<bool> alive_{true}, stop_{false};
    std::mutex mutex_;
    std::condition_variable space_;
    std::deque<std::string> chunks_;
    std::size_t queued_ = 0;
    std::string error_;
    std::thread reader_;
    msgpack::unpacker unpacker_;
    unsigned next_id_ = 0, input_id_ = 0;
    std::string pending_input_, active_input_;
};
}
