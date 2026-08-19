#pragma once

#include "common/Module.h"
#include "thread/Channel.h"
#include "thread/Task.h"
#include "thread/ThreadPool.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace eve {
namespace thread {

/**
 * Thread module: default pool, named channels, pool factory.
 * Script: eve.Thread() → getPool / newThreadPool / getChannel / newChannel.
 */
class Thread : public Module {
public:
    Module_REG(Thread);

    Thread();
    ~Thread() override;

    /** Hardware concurrency hint (at least 1). */
    int getHardwareConcurrency() const;

    /** Shared default pool (created lazily with hardwareConcurrency workers). */
    ThreadPool *getPool();

    /** Create an independent pool. Caller owns it (delete when done). */
    ThreadPool *newThreadPool(int workerCount = 0);

    /**
     * Named shared channel (love2d-style). Same name → same Channel instance
     * for the lifetime of the module.
     */
    Channel *getChannel(std::string name);

    /** Anonymous channel (not registered in the name map). Caller owns it. */
    Channel *newChannel();

    /**
     * Thread-safe post onto the Event module queue (any thread).
     * Main loop: event.pump is unrelated; just event.poll / pollData.
     */
    void postMain(std::string name, std::string data = "");

private:
    std::mutex mu_;
    std::unique_ptr<ThreadPool> defaultPool_;
    std::map<std::string, std::unique_ptr<Channel>> namedChannels_;
};

}  // namespace thread
}  // namespace eve
