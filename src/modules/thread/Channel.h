#pragma once
#include "common/Export.h"


#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

namespace eve {
namespace thread {

/**
 * @brief Thread-safe message queue (love2d-style Channel).
 * Values are strings so the API stays overload-free for Squirrel bindings.
 * Exposed to scripts as "ThreadChannel" (network already owns "Channel").
 */
class EVENGINE_API_FOUNDATION Channel {
public:
    /** @brief Channel. */
    Channel();
    /** @brief Channel. */
    explicit Channel(std::string name);
    /** @brief Channel. */
    ~Channel();

    /** @brief Returns the name. */
    std::string getName() const;

    /** @brief Pushes . */
    void push(std::string value);
    /** @brief Non-blocking pop; returns "" if empty. */
    std::string pop();
    /** @brief Block until a value is available, then pop it. */
    std::string demand();
    /** @brief Block up to timeoutMs; returns "" on timeout. */
    std::string supply(int timeoutMs);

    /** @brief True when data. */
    bool hasData() const;
    /** @brief Returns the count. */
    int getCount() const;
    /** @brief Clears . */
    void clear();

private:
    struct State {
        /** @brief Constructs a State. */
        explicit State(std::string channelName = {}) : name(std::move(channelName)) {}

        std::string name;
        mutable std::mutex mu;
        std::condition_variable cv;
        std::queue<std::string> queue;
    };

    std::shared_ptr<State> state_;

    friend class ThreadPool;
};

}  // namespace thread
}  // namespace eve
