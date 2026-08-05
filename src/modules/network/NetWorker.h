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

class NetWorker {
public:
    explicit NetWorker(Network* owner);
    ~NetWorker();

    void start();
    void stop();

    void post(NetCompletion c);
    void drain(std::vector<NetCompletion>& out);

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
