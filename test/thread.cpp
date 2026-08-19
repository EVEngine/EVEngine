#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Exception.h"
#include "thread/Channel.h"
#include "thread/JobSystem.h"
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

// ---- JobSystem ----

TEST_CASE("thread.jobsystem.create") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    REQUIRE(js.get() != nullptr);
    CHECK_EQ(js->getWorkerCount(), 2);
    CHECK(js->isRunning());
    CHECK_EQ(js->getOutstandingCount(), 0);
    js->waitAll();
    js->stop();
    CHECK(!js->isRunning());
    js->stop();  // idempotent
}

TEST_CASE("thread.jobsystem.submitWait") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<int> counter{0};
    std::vector<eve::thread::Job *> jobs;
    for (int i = 0; i < 16; ++i) {
        jobs.push_back(js->submit([&counter] {
            counter.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }));
    }
    for (auto *j : jobs) {
        REQUIRE(j != nullptr);
        j->wait();
        CHECK(j->isDone());
        delete j;
    }
    CHECK_EQ(counter.load(), 16);
    js->waitAll();
}

TEST_CASE("thread.jobsystem.parallelFor") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(4));
    constexpr int n = 1000;
    std::vector<int> data(n, -1);
    eve::thread::Job *loop = js->parallelFor(
        0, n, [&data](int first, int last) {
            for (int i = first; i < last; ++i) data[i] = i;
        },
        37);
    REQUIRE(loop != nullptr);
    loop->wait();
    CHECK(loop->isDone());
    for (int i = 0; i < n; ++i) CHECK_EQ(data[i], i);
    delete loop;
    js->waitAll();
}

TEST_CASE("thread.jobsystem.parallelForEmpty") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<int> calls{0};
    eve::thread::Job *loop = js->parallelFor(
        5, 5, [&calls](int, int) { calls.fetch_add(1, std::memory_order_relaxed); }, 4);
    REQUIRE(loop != nullptr);
    loop->wait();
    CHECK(loop->isDone());
    CHECK_EQ(calls.load(), 0);
    delete loop;
}

TEST_CASE("thread.jobsystem.parallelForAutoChunk") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<long long> sum{0};
    eve::thread::Job *loop = js->parallelFor(
        0, 10000,
        [&sum](int first, int last) {
            long long s = 0;
            for (int i = first; i < last; ++i) s += i;
            sum.fetch_add(s, std::memory_order_relaxed);
        },
        0);
    loop->wait();
    CHECK(sum.load() == 10000LL * 9999 / 2);
    delete loop;
}

TEST_CASE("thread.jobsystem.parallelForCompletion") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<int> callbacks{0};
    eve::thread::Job *loop = js->parallelFor(0, 100, [](int, int) {}, 10);
    loop->setCompletionCallback([&callbacks] {
        callbacks.fetch_add(1, std::memory_order_relaxed);
    });
    loop->wait();
    CHECK_EQ(callbacks.load(), 1);
    delete loop;
}

TEST_CASE("thread.jobsystem.taskGroup") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(3));
    std::atomic<int> counter{0};
    std::unique_ptr<eve::thread::TaskGroup> group(js->createTaskGroup());
    REQUIRE(group.get() != nullptr);
    for (int i = 0; i < 24; ++i) {
        group->fork([&counter] {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }
    group->wait();
    CHECK_EQ(group->getPendingCount(), 0);
    CHECK_EQ(counter.load(), 24);
}

TEST_CASE("thread.jobsystem.taskGroupReuse") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<int> counter{0};
    std::unique_ptr<eve::thread::TaskGroup> group(js->createTaskGroup());
    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 8; ++i)
            group->fork([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
        group->wait();
        CHECK_EQ(group->getPendingCount(), 0);
    }
    CHECK_EQ(counter.load(), 24);
}

TEST_CASE("thread.jobsystem.taskGroupCompletionCallbacks") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<int> counter{0};
    std::atomic<int> callbacks{0};
    std::unique_ptr<eve::thread::TaskGroup> group(js->createTaskGroup());
    for (int i = 0; i < 8; ++i) {
        group->fork(
            [&counter] { counter.fetch_add(1, std::memory_order_relaxed); },
            [&callbacks] { callbacks.fetch_add(1, std::memory_order_relaxed); });
    }
    group->wait();
    CHECK_EQ(counter.load(), 8);
    CHECK_EQ(callbacks.load(), 8);
}

TEST_CASE("thread.jobsystem.dependencies") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<int> produced{0};
    std::atomic<int> seen{0};
    eve::thread::Job *a = js->createJob([&produced] { produced.store(42); });
    eve::thread::Job *b = js->createJob([&produced, &seen] { seen.store(produced.load()); });
    b->addDependency(a);
    CHECK_EQ(b->getPendingDependencyCount(), 1);
    js->schedule(b);  // waits for a
    js->schedule(a);
    b->wait();
    a->wait();
    CHECK_EQ(seen.load(), 42);
    CHECK(a->isDone());
    CHECK(b->isDone());
    delete a;
    delete b;
}

TEST_CASE("thread.jobsystem.dependencyOnCompletedJobIsNoOp") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<int> counter{0};
    eve::thread::Job *a = js->submit([&counter] { counter.fetch_add(1); });
    a->wait();
    eve::thread::Job *b = js->createJob([&counter] { counter.fetch_add(1); });
    b->addDependency(a);  // a already done -> ignored
    CHECK_EQ(b->getPendingDependencyCount(), 0);
    js->schedule(b);
    b->wait();
    CHECK_EQ(counter.load(), 2);
    delete a;
    delete b;
}

TEST_CASE("thread.jobsystem.failedPredecessorStillReleasesDependent") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<int> bRuns{0};
    eve::thread::Job *a = js->createJob([] { throw eve::Exception("boom"); });
    eve::thread::Job *b = js->createJob([&bRuns] { bRuns.store(1); });
    b->addDependency(a);
    js->schedule(a);
    js->schedule(b);
    b->wait();
    a->wait();
    CHECK(a->hasFailed());
    CHECK(b->isDone());
    CHECK_EQ(bRuns.load(), 1);
    delete a;
    delete b;
}

TEST_CASE("thread.jobsystem.joinNode") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<int> runs{0};
    eve::thread::Job *leaf = js->createJob([&runs] { runs.fetch_add(1); });
    eve::thread::Job *join = js->createJob(eve::thread::JobFunc{});  // pure dependency node
    join->addDependency(leaf);
    js->schedule(leaf);
    js->schedule(join);
    join->wait();
    leaf->wait();
    CHECK_EQ(runs.load(), 1);
    delete leaf;
    delete join;
}

TEST_CASE("thread.jobsystem.completionCallback") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<int> bodyRan{0};
    std::atomic<int> callbackRan{0};
    eve::thread::Job *job = js->submit([&bodyRan] { bodyRan.store(1); });
    job->setCompletionCallback([&bodyRan, &callbackRan] {
        callbackRan.store(bodyRan.load());
    });
    job->wait();
    CHECK_EQ(callbackRan.load(), 1);
    delete job;
}

TEST_CASE("thread.jobsystem.failure") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(1));
    eve::thread::Job *job = js->submit([] { throw eve::Exception("kaboom"); });
    job->wait();
    CHECK(job->isDone());
    CHECK(job->hasFailed());
    CHECK_EQ(job->getError(), std::string("kaboom"));
    delete job;
}

TEST_CASE("thread.jobsystem.workerForkJoin") {
    // Fork/join from inside a pool worker must not deadlock even with a single
    // worker: the waiting worker helps execute the ready children.
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(1));
    std::atomic<int> counter{0};
    eve::thread::Job *outer = js->submit([&] {
        std::unique_ptr<eve::thread::TaskGroup> group(js->createTaskGroup());
        for (int i = 0; i < 32; ++i) {
            group->fork([&counter] {
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        }
        group->wait();
    });
    outer->wait();
    CHECK_EQ(counter.load(), 32);
    delete outer;
}

TEST_CASE("thread.jobsystem.workerParallelFor") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(1));
    std::atomic<long long> sum{0};
    eve::thread::Job *outer = js->submit([&] {
        eve::thread::Job *loop = js->parallelFor(
            0, 500,
            [&sum](int first, int last) {
                long long s = 0;
                for (int i = first; i < last; ++i) s += i;
                sum.fetch_add(s, std::memory_order_relaxed);
            },
            10);
        loop->wait();
        delete loop;
    });
    outer->wait();
    CHECK(sum.load() == 500LL * 499 / 2);
    delete outer;
}

TEST_CASE("thread.jobsystem.frameArena") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    for (int frame = 0; frame < 3; ++frame) {
        js->beginFrame();
        std::atomic<int> counter{0};
        std::vector<eve::thread::Job *> jobs;
        for (int i = 0; i < 64; ++i) {
            jobs.push_back(js->submitFrame([&counter] {
                counter.fetch_add(1, std::memory_order_relaxed);
            }));
        }
        for (auto *j : jobs) j->wait();
        js->endFrame();  // joins (already done) and recycles the arena
        CHECK_EQ(counter.load(), 64);
    }
    js->waitAll();
}

TEST_CASE("thread.jobsystem.frameParallelFor") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    js->beginFrame();
    std::vector<int> data(200, -1);
    eve::thread::Job *loop = js->parallelForFrame(
        0, 200,
        [&data](int first, int last) {
            for (int i = first; i < last; ++i) data[i] = i;
        },
        16);
    loop->wait();
    js->endFrame();
    for (int i = 0; i < 200; ++i) CHECK_EQ(data[i], i);
}

TEST_CASE("thread.jobsystem.frameTaskGroup") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    js->beginFrame();
    std::atomic<int> counter{0};
    eve::thread::TaskGroup *group = js->createFrameTaskGroup();
    for (int i = 0; i < 32; ++i) {
        group->fork([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
    }
    group->wait();
    CHECK_EQ(counter.load(), 32);
    js->endFrame();  // destroys the group and its arena-allocated children
}

TEST_CASE("thread.jobsystem.dependencyAfterScheduleRejected") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(1));
    eve::thread::Job *a = js->submit([] {});
    eve::thread::Job *b = js->createJob([] {});
    CHECK(expectException([&] { a->addDependency(b); }));
    CHECK(expectException([&] { a->addDependency(a); }));
    a->wait();
    delete a;
    js->schedule(b);
    b->wait();
    delete b;
}

TEST_CASE("thread.jobsystem.stopRejectsSchedule") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(1));
    eve::thread::Job *job = js->createJob([] {});
    js->stop();
    CHECK(!js->isRunning());
    CHECK(expectException([&] { js->schedule(job); }));
    CHECK(expectException([&] { js->submit([] {}); }));
    delete job;
}

TEST_CASE("thread.jobsystem.workerWaitAllRejected") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(1));
    std::atomic<bool> rejected{false};
    eve::thread::Job *job = js->submit([&] {
        try {
            js->waitAll();
        } catch (const eve::Exception &) {
            rejected.store(true, std::memory_order_relaxed);
        }
    });
    job->wait();
    CHECK(rejected.load(std::memory_order_relaxed));
    delete job;
}

TEST_CASE("thread.jobsystem.defaultSystem") {
    auto *jobs = threadModule()->getJobSystem();
    REQUIRE(jobs != nullptr);
    CHECK(jobs->getWorkerCount() >= 1);
    CHECK_EQ(jobs, threadModule()->getJobSystem());
    std::atomic<int> counter{0};
    eve::thread::Job *job = jobs->submit([&counter] {
        counter.fetch_add(1, std::memory_order_relaxed);
    });
    job->wait();
    CHECK_EQ(counter.load(), 1);
    delete job;
}
