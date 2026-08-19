#pragma once

#include "NetTypes.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace eve::network {

class Network;

/**
 * @brief Background thread that runs blocking socket I/O and HTTP jobs.
 * Completions are queued and drained by Network::pump on the main thread.
 */
class NetWorker {
public:
    /** @brief Creates a worker owned by `owner` (not started yet). */
    explicit NetWorker(Network* owner);
    ~NetWorker();

    /** @brief Starts the worker thread. */
    void start();
    /** @brief Stops the worker thread and joins it. */
    void stop();

    /** @brief Queues a completion for the main thread (thread-safe). */
    void post(NetCompletion c);
    /** @brief Test helper: moves all pending completions into out. */
    void drain(std::vector<NetCompletion>& out);

    /** @brief Queues an arbitrary blocking job to run on the worker thread. */
    void submit(std::function<void()> job);

private:
    void threadMain();

    Network* owner_ = nullptr;
    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<NetCompletion> completions_;
    std::vector<std::function<void()>> jobs_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace eve::network
