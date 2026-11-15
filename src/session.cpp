#include "session.hpp"
#include <map>
#include <cstring>
namespace imvim {
Session::Session(const std::string& executable, const std::vector<std::string>& args, std::function<void()> wake)
    : process_(executable, args), wake_(std::move(wake)), reader_([this] { read_loop(); }) {}
Session::~Session() {
    stop_ = true; space_.notify_all(); process_.stop();
    if (reader_.joinable()) reader_.join();
}
void Session::read_loop() {
    try {
        char data[16384];
        while (!stop_) {
            auto n = process_.read(data, sizeof(data));
            if (!n) break;
            {
                std::unique_lock lock(mutex_);
                space_.wait(lock, [&] { return stop_ || queued_ < 4 * 1024 * 1024; });
                if (stop_) break;
                chunks_.emplace_back(data, n); queued_ += n;
            }
            if (wake_) wake_();
        }
    } catch (const std::exception& e) {
        std::lock_guard lock(mutex_); error_ = e.what();
    }
    alive_ = false;
    if (wake_) wake_();
}
std::vector<msgpack::object_handle> Session::poll() {
    std::deque<std::string> chunks;
    {
        std::lock_guard lock(mutex_);
        if (!error_.empty()) throw std::runtime_error(error_);
        chunks.swap(chunks_); queued_ = 0;
    }
    space_.notify_one();
    std::vector<msgpack::object_handle> messages;
    for (const auto& chunk : chunks) {
        unpacker_.reserve_buffer(chunk.size());
        std::memcpy(unpacker_.buffer(), chunk.data(), chunk.size());
        unpacker_.buffer_consumed(chunk.size());
        msgpack::object_handle message;
        while (unpacker_.next(message)) {
            const auto& o = message.get();
            if (o.type == msgpack::type::ARRAY && o.via.array.size == 4 && o.via.array.ptr[0].as<int>() == 1) {
                const auto* a = o.via.array.ptr;
                if (input_id_ && a[1].as<unsigned>() == input_id_) {
                    if (a[2].is_nil()) {
                        auto consumed = a[3].as<std::size_t>();
                        if (consumed > active_input_.size()) throw std::runtime_error("Invalid input acknowledgement");
                        pending_input_.insert(0, active_input_.substr(consumed));
                    }
                    active_input_.clear(); input_id_ = 0;
                }
            }
            messages.push_back(std::move(message));
        }
        if (unpacker_.nonparsed_size() > 16 * 1024 * 1024) throw std::runtime_error("Neovim message exceeds 16 MiB limit");
    }
    if (alive_) send_input();
    return messages;
}
void Session::attach(int columns, int rows) {
    request("nvim_ui_attach", std::make_tuple(columns, rows, std::map<std::string, bool>{{"rgb", true}, {"ext_linegrid", true}}));
}
void Session::send_input() {
    if (input_id_ || pending_input_.empty()) return;
    // Keep complete UTF-8 and <key> notation intact. UI events are small;
    // clipboard text uses nvim_paste instead of this queue.
    active_input_.swap(pending_input_);
    input_id_ = request("nvim_input", std::make_tuple(active_input_));
}
void Session::input(std::string text) {
    if (pending_input_.size() + text.size() > 1024 * 1024) throw std::runtime_error("Keyboard input queue exceeded 1 MiB");
    pending_input_ += text;
    send_input();
}
std::string Session::escape_text(const std::string& text) {
    std::string result;
    for (char c : text) result += c == '<' ? "<LT>" : std::string(1, c);
    return result;
}
void Session::text(const std::string& text) { input(escape_text(text)); }
void Session::paste(const std::string& text) {
    if (text.size() > 8 * 1024 * 1024) throw std::runtime_error("Paste exceeds prototype's 8 MiB limit");
    request("nvim_paste", std::make_tuple(text, true, -1));
}
}
