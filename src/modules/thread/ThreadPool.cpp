#include "thread/ThreadPool.h"

#include "common/Exception.h"
#include "common/Module.h"
#include "event/Event.h"
#include "thread/Channel.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <system_error>
#include <utility>

namespace eve {
namespace thread {

namespace {
constexpr int kMaxWorkerCount = 256;
thread_local const void *currentPoolState = nullptr;
std::mutex eventResolveMu;
}  // namespace

struct ThreadPool::State {
    mutable std::mutex mu;
    std::condition_variable cv;
    std::condition_variable idleCv;
    std::queue<std::shared_ptr<Task::State>> queue;
    int busy = 0;
    bool stopping = false;
};

ThreadPool::ThreadPool(int workerCount) {
    if (workerCount <= 0) {
        workerCount = static_cast<int>(std::thread::hardware_concurrency());
        if (workerCount <= 0)
            workerCount = 1;
    }
    if (workerCount > kMaxWorkerCount)
        throw eve::Exception("ThreadPool worker count exceeds limit (%d)", kMaxWorkerCount);

    workerCount_ = workerCount;
    state_ = std::make_shared<State>();
    workers_.reserve(static_cast<size_t>(workerCount_));
    try {
        for (int i = 0; i < workerCount_; ++i) {
            auto state = state_;
            workers_.emplace_back([state] { workerMain(state); });
        }
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(state_->mu);
            state_->stopping = true;
        }
        state_->cv.notify_all();
        for (auto &worker : workers_) {
            if (worker.joinable())
                worker.join();
        }
        workers_.clear();
        throw;
    }
}

ThreadPool::~ThreadPool() {
    try {
        stopImpl(true);
    } catch (...) {
    }
}

int ThreadPool::getWorkerCount() const { return workerCount_; }

int ThreadPool::getPendingCount() const {
    std::lock_guard<std::mutex> lock(state_->mu);
    return static_cast<int>(state_->queue.size());
}

bool ThreadPool::isRunning() const {
    std::lock_guard<std::mutex> lock(state_->mu);
    return !state_->stopping;
}

Task *ThreadPool::submit(std::function<void()> fn) {
    if (!fn)
        throw eve::Exception("ThreadPool::submit: null function");

    auto state = std::make_shared<Task::State>(std::move(fn));
    auto *task = new Task(state);
    {
        std::lock_guard<std::mutex> lock(state_->mu);
        if (state_->stopping) {
            delete task;
            throw eve::Exception("ThreadPool is stopped");
        }
        state_->queue.push(std::move(state));
    }
    state_->cv.notify_one();
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
    auto channelState = channel->state_;
    return submit([channelState, msg = std::move(message), delayMs] {
        if (delayMs > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        {
            std::lock_guard<std::mutex> lock(channelState->mu);
            channelState->queue.push(msg);
        }
        channelState->cv.notify_one();
    });
}

Task *ThreadPool::submitPost(std::string name, std::string data, int delayMs) {
    if (name.empty())
        throw eve::Exception("ThreadPool::submitPost: name must not be empty");
    if (delayMs < 0)
        delayMs = 0;
    event::Event *ev = nullptr;
    {
        // ModuleManager is not thread-safe. Resolve/create the singleton while
        // submitPost is still on the submitting (script/main) thread.
        std::lock_guard<std::mutex> lock(eventResolveMu);
        ev = ModuleManager::getInstance<event::Event>("Event");
        if (!ev)
            ev = event::Event::create();
    }
    return submit([ev, name = std::move(name), data = std::move(data), delayMs] {
        if (delayMs > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        ev->pushData(name, data);
    });
}

void ThreadPool::waitAll() {
    auto state = state_;
    if (currentPoolState == state.get())
        throw eve::Exception("ThreadPool::waitAll cannot be called from its worker");
    std::unique_lock<std::mutex> lock(state->mu);
    state->idleCv.wait(lock, [&state] { return state->queue.empty() && state->busy == 0; });
}

void ThreadPool::stop() {
    if (currentPoolState == state_.get())
        throw eve::Exception("ThreadPool::stop cannot be called from its worker");
    stopImpl(false);
}

void ThreadPool::stopImpl(bool allowWorkerCaller) {
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMu_);
    auto state = state_;
    const bool calledByWorker = currentPoolState == state.get();
    if (calledByWorker && !allowWorkerCaller)
        throw eve::Exception("ThreadPool::stop cannot be called from its worker");

    {
        std::lock_guard<std::mutex> lock(state->mu);
        state->stopping = true;
    }
    state->cv.notify_all();

    // Destruction from a running job cannot safely join any pool worker: a
    // different worker may itself be waiting for the current Task to finish.
    // Workers only retain State, not ThreadPool, so detaching all of them here
    // is safe and lets them drain accepted work before exiting.
    if (calledByWorker) {
        for (auto &worker : workers_) {
            if (worker.joinable())
                worker.detach();
        }
        workers_.clear();
        return;
    }

    for (auto &w : workers_) {
        if (!w.joinable())
            continue;
        try {
            w.join();
        } catch (const std::system_error &) {
            if (w.joinable())
                w.detach();
        }
    }
    workers_.clear();
}

void ThreadPool::workerMain(std::shared_ptr<State> state) {
    currentPoolState = state.get();
    for (;;) {
        std::shared_ptr<Task::State> task;
        {
            std::unique_lock<std::mutex> lock(state->mu);
            state->cv.wait(lock, [&state] { return state->stopping || !state->queue.empty(); });
            if (state->stopping && state->queue.empty()) {
                currentPoolState = nullptr;
                return;
            }
            task = std::move(state->queue.front());
            state->queue.pop();
            ++state->busy;
        }

        if (task)
            Task::run(task);

        {
            std::lock_guard<std::mutex> lock(state->mu);
            --state->busy;
            if (state->queue.empty() && state->busy == 0)
                state->idleCv.notify_all();
        }
    }
}

}  // namespace thread
}  // namespace eve
