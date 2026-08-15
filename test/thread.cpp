#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Exception.h"
#include "thread/Channel.h"
#include "thread/Task.h"
#include "thread/Thread.h"
#include "thread/ThreadPool.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

eve::thread::Thread *threadModule() { return eve::thread::Thread::create(); }

bool expectException(const std::function<void()> &fn) {
    try {
        fn();
    } catch (const eve::Exception &) {
        return true;
    }
    return false;
}

}  // namespace

TEST_CASE("thread.create") {
    auto *mod = threadModule();
    REQUIRE(mod != nullptr);
    CHECK_EQ(mod->getName(), std::string("Thread"));
    CHECK(mod->getHardwareConcurrency() >= 1);
}

TEST_CASE("thread.channel.pushPop") {
    std::unique_ptr<eve::thread::Channel> ch(threadModule()->newChannel());
    REQUIRE(ch.get() != nullptr);
    CHECK(!ch->hasData());
    CHECK_EQ(ch->getCount(), 0);

    std::string empty = ch->pop();
    CHECK_EQ(empty, std::string(""));

    ch->push("a");
    ch->push("b");
    CHECK(ch->hasData());
    CHECK_EQ(ch->getCount(), 2);
    std::string first = ch->pop();
    std::string second = ch->pop();
    CHECK_EQ(first, std::string("a"));
    CHECK_EQ(second, std::string("b"));
    CHECK(!ch->hasData());
}

TEST_CASE("thread.channel.namedShared") {
    auto *mod = threadModule();
    auto *a = mod->getChannel("results");
    auto *b = mod->getChannel("results");
    REQUIRE(a != nullptr);
    CHECK_EQ(a, b);
    CHECK_EQ(a->getName(), std::string("results"));
    a->clear();
    a->push("ok");
    std::string got = b->pop();
    CHECK_EQ(got, std::string("ok"));
    CHECK(expectException([&] { mod->getChannel(""); }));
}

TEST_CASE("thread.channel.supplyTimeout") {
    std::unique_ptr<eve::thread::Channel> ch(threadModule()->newChannel());
    auto t0 = std::chrono::steady_clock::now();
    std::string got = ch->supply(30);
    CHECK_EQ(got, std::string(""));
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0)
                  .count();
    CHECK(ms >= 20);
}

TEST_CASE("thread.channel.crossThread") {
    std::unique_ptr<eve::thread::Channel> ch(threadModule()->newChannel());
    std::thread producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        ch->push("from-worker");
    });
    std::string got = ch->supply(1000);
    producer.join();
    CHECK_EQ(got, std::string("from-worker"));
}

TEST_CASE("thread.pool.submitSleep") {
    std::unique_ptr<eve::thread::ThreadPool> pool(threadModule()->newThreadPool(2));
    REQUIRE(pool.get() != nullptr);
    CHECK_EQ(pool->getWorkerCount(), 2);
    CHECK(pool->isRunning());

    std::unique_ptr<eve::thread::Task> task(pool->submitSleep(20));
    REQUIRE(task.get() != nullptr);
    task->wait();
    CHECK(task->isDone());
    CHECK(!task->hasFailed());
    CHECK_EQ(task->getStatus(), std::string("done"));
    pool->waitAll();
}

TEST_CASE("thread.pool.submitPush") {
    auto *mod = threadModule();
    std::unique_ptr<eve::thread::Channel> ch(mod->newChannel());

    std::unique_ptr<eve::thread::ThreadPool> pool(mod->newThreadPool(2));
    std::unique_ptr<eve::thread::Task> task(pool->submitPush(ch.get(), "hello", 10));
    REQUIRE(task.get() != nullptr);
    task->wait();
    CHECK_EQ(task->getStatus(), std::string("done"));
    CHECK(!task->hasFailed());
    std::string got = ch->supply(500);
    CHECK_EQ(got, std::string("hello"));
}

TEST_CASE("thread.pool.parallelWork") {
    std::unique_ptr<eve::thread::ThreadPool> pool(threadModule()->newThreadPool(4));
    std::atomic<int> counter{0};
    std::vector<eve::thread::Task *> tasks;
    for (int i = 0; i < 32; ++i) {
        tasks.push_back(pool->submit([&counter] {
            counter.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }));
    }
    pool->waitAll();
    for (auto *t : tasks) {
        REQUIRE(t != nullptr);
        CHECK(t->isDone());
        delete t;
    }
    CHECK_EQ(counter.load(), 32);
}

TEST_CASE("thread.pool.taskFailure") {
    std::unique_ptr<eve::thread::ThreadPool> pool(threadModule()->newThreadPool(1));
    std::unique_ptr<eve::thread::Task> task(pool->submit([] {
        throw eve::Exception("boom");
    }));
    task->wait();
    CHECK(task->hasFailed());
    CHECK_EQ(task->getStatus(), std::string("failed"));
    CHECK_EQ(task->getError(), std::string("boom"));
}

TEST_CASE("thread.defaultPool") {
    auto *pool = threadModule()->getPool();
    REQUIRE(pool != nullptr);
    CHECK(pool->getWorkerCount() >= 1);
    CHECK_EQ(pool, threadModule()->getPool());
}

TEST_CASE("thread.pool.stopRejectsSubmit") {
    std::unique_ptr<eve::thread::ThreadPool> pool(threadModule()->newThreadPool(1));
    pool->stop();
    CHECK(!pool->isRunning());
    CHECK(expectException([&] { pool->submitSleep(1); }));
}

TEST_CASE("thread.pool.queueOwnsTaskState") {
    std::unique_ptr<eve::thread::ThreadPool> pool(threadModule()->newThreadPool(1));
    eve::thread::Task *task = pool->submitSleep(10);
    delete task;
    pool->waitAll();
    CHECK_EQ(pool->getPendingCount(), 0);
}

TEST_CASE("thread.pool.jobOwnsAnonymousChannelState") {
    std::unique_ptr<eve::thread::ThreadPool> pool(threadModule()->newThreadPool(1));
    auto *channel = threadModule()->newChannel();
    std::unique_ptr<eve::thread::Task> task(pool->submitPush(channel, "late", 10));
    delete channel;
    task->wait();
    CHECK_EQ(task->getStatus(), std::string("done"));
}

TEST_CASE("thread.pool.workerWaitAllRejected") {
    std::unique_ptr<eve::thread::ThreadPool> pool(threadModule()->newThreadPool(1));
    std::atomic<bool> rejected{false};
    std::unique_ptr<eve::thread::Task> task(pool->submit([&] {
        try {
            pool->waitAll();
        } catch (const eve::Exception &) {
            rejected.store(true, std::memory_order_relaxed);
        }
    }));
    task->wait();
    CHECK(rejected.load(std::memory_order_relaxed));
}

TEST_CASE("thread.pool.workerStopRejected") {
    std::unique_ptr<eve::thread::ThreadPool> pool(threadModule()->newThreadPool(1));
    std::atomic<bool> rejected{false};
    std::unique_ptr<eve::thread::Task> task(pool->submit([&] {
        try {
            pool->stop();
        } catch (const eve::Exception &) {
            rejected.store(true, std::memory_order_relaxed);
        }
    }));
    task->wait();
    CHECK(rejected.load(std::memory_order_relaxed));
    CHECK(pool->isRunning());
}

TEST_CASE("thread.pool.workerCountBounded") {
    CHECK(expectException([&] {
        std::unique_ptr<eve::thread::ThreadPool> pool(threadModule()->newThreadPool(257));
    }));
}

TEST_CASE("thread.pool.destructorFromWorkerIsSafe") {
    auto *pool = threadModule()->newThreadPool(1);
    std::unique_ptr<eve::thread::Task> task(pool->submit([pool] { delete pool; }));
    task->wait();
    CHECK_EQ(task->getStatus(), std::string("done"));
}

TEST_CASE("thread.pool.workerDestructorDoesNotJoinDependentWorker") {
    auto *pool = threadModule()->newThreadPool(2);
    std::atomic<eve::thread::Task *> deletingTask{nullptr};
    std::atomic<bool> waiterReady{false};

    std::unique_ptr<eve::thread::Task> waiter(pool->submit([&] {
        eve::thread::Task *target = nullptr;
        while ((target = deletingTask.load(std::memory_order_acquire)) == nullptr)
            std::this_thread::yield();
        waiterReady.store(true, std::memory_order_release);
        target->wait();
    }));
    std::unique_ptr<eve::thread::Task> destroyer(pool->submit([&] {
        while (!waiterReady.load(std::memory_order_acquire))
            std::this_thread::yield();
        delete pool;
    }));
    deletingTask.store(destroyer.get(), std::memory_order_release);

    destroyer->wait();
    waiter->wait();
    CHECK_EQ(destroyer->getStatus(), std::string("done"));
    CHECK_EQ(waiter->getStatus(), std::string("done"));
}
