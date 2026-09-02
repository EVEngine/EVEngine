#include "thread/Thread.h"

#include "common/AsyncWork.h"
#include "common/Diagnostic.h"
#include "common/Exception.h"
#include "common/Capability.h"
#include "common/MainThreadPost.h"
#include "common/Result.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>
#include <thread>

namespace eve {
namespace thread {

namespace {

class PoolExecutor final : public eve::caps::IAsyncWorkExecutor {
public:
    explicit PoolExecutor(Thread *owner) : owner_(owner) {}

    eve::Result<void> submit(std::function<void()> work) override {
        if (!work) {
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "IAsyncWorkExecutor: null work"));
        }
        try {
            std::unique_ptr<Task> task(owner_->getPool()->submit(std::move(work)));
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
        } catch (const eve::Exception &ex) {
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, ex.what()));
        }
    }

private:
    Thread *owner_ = nullptr;
};

}  // namespace

Module_IMPL(Thread, new Thread());

Thread::Thread() {
    executor_ = std::make_unique<PoolExecutor>(this);
    cap::provide<caps::IAsyncWorkExecutor>(executor_.get());
}

Thread::~Thread() {
    cap::revoke<caps::IAsyncWorkExecutor>(executor_.get());
    if (defaultPool_)
        defaultPool_->stop();
    if (defaultJobSystem_)
        defaultJobSystem_->stop();
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

JobSystem *Thread::getJobSystem() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!defaultJobSystem_)
        defaultJobSystem_ = std::unique_ptr<JobSystem>(createJobSystem(getHardwareConcurrency()));
    return defaultJobSystem_.get();
}

Channel *Thread::getChannel(std::string channelName) {
    if (channelName.empty())
        throw eve::Exception("Thread::getChannel: name must not be empty");
    std::lock_guard<std::mutex> lock(mu_);
    auto it = namedChannels_.find(channelName);
    if (it != namedChannels_.end())
        return it->second.get();
    auto ch = std::make_unique<Channel>(channelName);
    Channel *raw = ch.get();
    namedChannels_.emplace(std::move(channelName), std::move(ch));
    return raw;
}

Channel *Thread::newChannel() { return new Channel(); }

void Thread::postMain(std::string eventName, std::string data) {
    if (eventName.empty())
        throw eve::Exception("Thread::postMain: name must not be empty");
    auto *poster = cap::query<caps::IMainThreadPost>();
    if (!poster)
        throw eve::Exception("Thread::postMain: no main-thread queue (event module not linked)");
    poster->prepare();
    poster->postToMainThread(std::move(eventName), std::move(data));
}

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
    pool.addFunc("submitPost", &ThreadPool::submitPost);
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
    cls.addFunc("postMain", &Thread::postMain);
}

}  // namespace thread
}  // namespace eve
