#include "thread/Thread.h"

#include "common/Exception.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>
#include <thread>

namespace eve {
namespace thread {

Module_IMPL(Thread, new Thread());

Thread::Thread() = default;

Thread::~Thread() {
    if (defaultPool_)
        defaultPool_->stop();
}

int Thread::getHardwareConcurrency() const {
    unsigned n = std::thread::hardware_concurrency();
    return n == 0 ? 1 : static_cast<int>(n);
}

ThreadPool *Thread::getPool() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!defaultPool_)
        defaultPool_ = std::make_unique<ThreadPool>(getHardwareConcurrency());
    return defaultPool_.get();
}

ThreadPool *Thread::newThreadPool(int workerCount) {
    if (workerCount < 0)
        throw eve::Exception("Thread::newThreadPool: workerCount must be >= 0");
    return new ThreadPool(workerCount);
}

Channel *Thread::getChannel(std::string name) {
    if (name.empty())
        throw eve::Exception("Thread::getChannel: name must not be empty");
    std::lock_guard<std::mutex> lock(mu_);
    auto it = namedChannels_.find(name);
    if (it != namedChannels_.end())
        return it->second.get();
    auto ch = std::make_unique<Channel>(name);
    Channel *raw = ch.get();
    namedChannels_.emplace(std::move(name), std::move(ch));
    return raw;
}

Channel *Thread::newChannel() { return new Channel(); }

void Thread::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Thread::create, false);
    expose(cls);

    // Avoid clashing with network::Channel ("Channel").
    auto ch = table.addClass<Channel>(
        "ThreadChannel", std::function<Channel *()>([]() -> Channel * { return nullptr; }), true);
    ch.addFunc("getName", &Channel::getName);
    ch.addFunc("push", &Channel::push);
    ch.addFunc("pop", &Channel::pop);
    ch.addFunc("demand", &Channel::demand);
    ch.addFunc("supply", &Channel::supply);
    ch.addFunc("hasData", &Channel::hasData);
    ch.addFunc("getCount", &Channel::getCount);
    ch.addFunc("clear", &Channel::clear);

    auto task = table.addClass<Task>(
        "Task", std::function<Task *()>([]() -> Task * { return nullptr; }), true);
    task.addFunc("getStatus", &Task::getStatus);
    task.addFunc("isDone", &Task::isDone);
    task.addFunc("hasFailed", &Task::hasFailed);
    task.addFunc("getError", &Task::getError);
    task.addFunc("wait", &Task::wait);

    auto pool = table.addClass<ThreadPool>(
        "ThreadPool", std::function<ThreadPool *()>([]() -> ThreadPool * { return nullptr; }), true);
    pool.addFunc("getWorkerCount", &ThreadPool::getWorkerCount);
    pool.addFunc("getPendingCount", &ThreadPool::getPendingCount);
    pool.addFunc("isRunning", &ThreadPool::isRunning);
    pool.addFunc("submitSleep", &ThreadPool::submitSleep);
    pool.addFunc("submitPush", &ThreadPool::submitPush);
    pool.addFunc("waitAll", &ThreadPool::waitAll);
    pool.addFunc("stop", &ThreadPool::stop);
}

void Thread::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Thread::getName);
    cls.addFunc("getHardwareConcurrency", &Thread::getHardwareConcurrency);
    cls.addFunc("getPool", &Thread::getPool);
    cls.addFunc("newThreadPool", &Thread::newThreadPool);
    cls.addFunc("getChannel", &Thread::getChannel);
    cls.addFunc("newChannel", &Thread::newChannel);
}

}  // namespace thread
}  // namespace eve
