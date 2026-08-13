#include "thread/ThreadPool.h"

#include "common/Exception.h"
#include "common/Module.h"
#include "event/Event.h"
#include "thread/Channel.h"

#include <algorithm>
#include <chrono>
#include <system_error>
#include <utility>

namespace eve {
namespace thread {

ThreadPool::ThreadPool(int workerCount) {
    if (workerCount <= 0) {
        workerCount = static_cast<int>(std::thread::hardware_concurrency());
        if (workerCount <= 0)
            workerCount = 1;
    }
    workerCount_ = workerCount;
    workers_.reserve(static_cast<size_t>(workerCount_));
    for (int i = 0; i < workerCount_; ++i)
        workers_.emplace_back([this] { workerMain(); });
}

ThreadPool::~ThreadPool() {
    try {
        stop();
    } catch (...) {
    }
}

int ThreadPool::getWorkerCount() const { return workerCount_; }

int ThreadPool::getPendingCount() const {
    std::lock_guard<std::mutex> lock(mu_);
    return static_cast<int>(queue_.size());
}

bool ThreadPool::isRunning() const {
    std::lock_guard<std::mutex> lock(mu_);
    return !stopping_;
}

Task *ThreadPool::submit(std::function<void()> fn) {
    if (!fn)
        throw eve::Exception("ThreadPool::submit: null function");

    auto *task = new Task(std::move(fn));
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (stopping_) {
            delete task;
            throw eve::Exception("ThreadPool is stopped");
        }
        queue_.push(task);
    }
    cv_.notify_one();
    return task;
}

Task *ThreadPool::submitSleep(int ms) {
    if (ms < 0)
        ms = 0;
    return submit([ms] {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    });
}

Task *ThreadPool::submitPush(Channel *channel, std::string message, int delayMs) {
    if (channel == nullptr)
        throw eve::Exception("ThreadPool::submitPush: channel is null");
    if (delayMs < 0)
        delayMs = 0;
    return submit([channel, msg = std::move(message), delayMs] {
        if (delayMs > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        channel->push(msg);
    });
}

Task *ThreadPool::submitPost(std::string name, std::string data, int delayMs) {
    if (name.empty())
        throw eve::Exception("ThreadPool::submitPost: name must not be empty");
    if (delayMs < 0)
        delayMs = 0;
    return submit([name = std::move(name), data = std::move(data), delayMs] {
        if (delayMs > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        auto *ev = ModuleManager::getInstance<event::Event>("Event");
        if (!ev)
            ev = event::Event::create();
        ev->pushData(name, data);
    });
}

void ThreadPool::waitAll() {
    std::unique_lock<std::mutex> lock(mu_);
    idleCv_.wait(lock, [this] { return queue_.empty() && busy_ == 0; });
}

void ThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (stopping_)
            return;
        stopping_ = true;
    }
    cv_.notify_all();
    const auto self = std::this_thread::get_id();
    for (auto &w : workers_) {
        if (!w.joinable())
            continue;
        // MSVC throws std::system_error(resource_deadlock_would_occur) if this
        // thread is a worker (stop from a task / TLS teardown).
        if (w.get_id() == self) {
            w.detach();
            continue;
        }
        try {
            w.join();
        } catch (const std::system_error &) {
            if (w.joinable())
                w.detach();
        }
    }
    workers_.clear();
}

void ThreadPool::workerMain() {
    for (;;) {
        Task *task = nullptr;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty())
                return;
            task = queue_.front();
            queue_.pop();
            ++busy_;
        }

        if (task)
            task->run();

        {
            std::lock_guard<std::mutex> lock(mu_);
            --busy_;
            if (queue_.empty() && busy_ == 0)
                idleCv_.notify_all();
        }
    }
}

}  // namespace thread
}  // namespace eve
