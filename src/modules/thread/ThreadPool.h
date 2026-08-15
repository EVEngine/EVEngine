#pragma once

#include "thread/Task.h"

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace eve {
namespace thread {

class Channel;

/**
 * Fixed-size worker pool. Owns worker std::threads; tasks run FIFO.
 * Squirrel VM is not thread-safe — do not call into scripts from workers.
 */
class ThreadPool {
public:
    explicit ThreadPool(int workerCount);
    ~ThreadPool();

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    int getWorkerCount() const;
    int getPendingCount() const;
    bool isRunning() const;

    /** Submit a C++ callable. Caller owns the Task wrapper; work owns its shared state. */
    Task *submit(std::function<void()> fn);

    /** Sleep on a worker, then mark done — useful from scripts / tests. */
    Task *submitSleep(int ms);

    /** Sleep, then push a message onto a channel (cross-thread signalling). */
    Task *submitPush(Channel *channel, std::string message, int delayMs = 0);

    /**
     * Sleep, then post an Event on the main queue (thread-safe).
     * Scripts poll via event.poll / event.pollData, or async helpers.
     */
    Task *submitPost(std::string name, std::string data = "", int delayMs = 0);

    /** Block until idle. Throws when called by a worker belonging to this pool. */
    void waitAll();

    /** Stop accepting work and join workers. Worker calls are rejected. Idempotent. */
    void stop();

private:
    struct State;

    static void workerMain(std::shared_ptr<State> state);
    void stopImpl(bool allowWorkerCaller);

    int workerCount_ = 0;
    std::vector<std::thread> workers_;
    std::shared_ptr<State> state_;
    std::mutex lifecycleMu_;
};

}  // namespace thread
}  // namespace eve
