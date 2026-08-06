#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>

namespace eve {
namespace thread {

/**
 * Thread-safe message queue (love2d-style Channel).
 * Values are strings so the API stays overload-free for Squirrel bindings.
 * Exposed to scripts as "ThreadChannel" (network already owns "Channel").
 */
class Channel {
public:
    Channel() = default;
    explicit Channel(std::string name);
    ~Channel();

    std::string getName() const;

    void push(std::string value);
    /** Non-blocking pop; returns "" if empty. */
    std::string pop();
    /** Block until a value is available, then pop it. */
    std::string demand();
    /** Block up to timeoutMs; returns "" on timeout. */
    std::string supply(int timeoutMs);

    bool hasData() const;
    int getCount() const;
    void clear();

private:
    std::string name_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::queue<std::string> queue_;
};

}  // namespace thread
}  // namespace eve
