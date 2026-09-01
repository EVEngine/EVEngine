#pragma once

#include "common/Module.h"
#include "common/AsyncWork.h"
#include "thread/Channel.h"
#include "thread/JobSystem.h"
#include "thread/Task.h"
#include "thread/ThreadPool.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace eve {
namespace thread {

/**
 * @brief Thread module: default pool, named channels, pool factory.
 * Script: eve.Thread() → getPool / newThreadPool / getChannel / newChannel.
 */
class Thread : public Module {
public:
    Module_REG(Thread);

    Thread();
    ~Thread() override;

    /** @brief Hardware concurrency hint (at least 1). */
    int getHardwareConcurrency() const;

    /** @brief Shared default pool (created lazily with hardwareConcurrency workers). */
    ThreadPool *getPool();

    /** @brief Create an independent pool. Caller owns it (delete when done). */
    ThreadPool *newThreadPool(int workerCount = 0);

    /**
     * @brief Shared engine-wide JobSystem (created lazily with hardware
     * concurrency workers). Use it for dependency graphs, parallel_for and
     * frame-scoped jobs; the pool below remains for simple async work.
     */
    JobSystem *getJobSystem();

    /**
     * @brief Named shared channel (love2d-style). Same name → same Channel instance
     * for the lifetime of the module.
     */
    Channel *getChannel(std::string channelName);

    /** @brief Anonymous channel (not registered in the name map). Caller owns it. */
    Channel *newChannel();

    /**
     * @brief Thread-safe post onto the Event module queue (any thread).
     * Main loop: event.pump is unrelated; just event.poll / pollData.
     */
    void postMain(std::string eventName, std::string data = "");

private:
    std::mutex mu_;
    std::unique_ptr<ThreadPool> defaultPool_;
    std::unique_ptr<JobSystem> defaultJobSystem_;
    std::unique_ptr<eve::caps::IAsyncWorkExecutor> executor_;
    std::map<std::string, std::unique_ptr<Channel>> namedChannels_;
};

}  // namespace thread
}  // namespace eve
