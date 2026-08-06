#pragma once

#include "thread/Task.h"

#include <condition_variable>
#include <cstddef>
#include <functional>
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

    /** Submit a C++ callable. Caller owns the returned Task* (delete when done). */
    Task *submit(std::function<void()> fn);

    /** Sleep on a worker, then mark done — useful from scripts / tests. */
    Task *submitSleep(int ms);

    /** Sleep, then push a message onto a channel (cross-thread signalling). */
    Task *submitPush(Channel *channel, std::string message, int delayMs = 0);

    /** Block until the queue is empty and no worker is busy. */
    void waitAll();

    /** Stop accepting work, join workers. Idempotent. */
    void stop();

private:
    void workerMain();

    int workerCount_ = 0;
    std::vector<std::thread> workers_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::condition_variable idleCv_;
    std::queue<Task *> queue_;
    int busy_ = 0;
    bool stopping_ = false;
};

}  // namespace thread
}  // namespace eve
