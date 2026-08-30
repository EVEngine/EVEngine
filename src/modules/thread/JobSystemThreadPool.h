#pragma once

#include "thread/JobSystem.h"
#include "thread/Task.h"

#include <memory>
#include <mutex>
#include <vector>

namespace eve {
namespace thread {

class ThreadPool;

/**
 * @brief Default JobSystem backend: a dependency-aware scheduler over the
 * existing ThreadPool worker pool.
 *
 * The ThreadPool keeps its FIFO + CV worker model unchanged; this class uses it
 * purely as the worker-thread provider. Job scheduling (dependencies, ready
 * queue, fork/join help-execution, per-frame arena) lives here, so replacing
 * the backend with TBB does not touch any engine code that uses JobSystem.
 */
class JobSystemThreadPool final : public JobSystem {
public:
    /** @brief Opaque scheduler state (defined in JobSystemThreadPool.cpp). */
    struct State;

    explicit JobSystemThreadPool(int workerCount);
    ~JobSystemThreadPool() override;

    JobSystemThreadPool(const JobSystemThreadPool &) = delete;
    JobSystemThreadPool &operator=(const JobSystemThreadPool &) = delete;

    int getWorkerCount() const override;
    bool isRunning() const override;
    int getPendingCount() const override;
    int getOutstandingCount() const override;

    Job *submit(JobFunc body) override;
    Job *createJob(JobFunc body) override;
    void schedule(Job *job) override;
    Job *parallelFor(int first, int last, ParallelForBody body, int chunk = 1) override;
    TaskGroup *createTaskGroup() override;

    Job *submitFrame(JobFunc body) override;
    Job *createFrameJob(JobFunc body) override;
    Job *parallelForFrame(int first, int last, ParallelForBody body, int chunk = 1) override;
    TaskGroup *createFrameTaskGroup() override;
    void beginFrame() override;
    void endFrame() override;

    void waitAll() override;
    void stop() override;

private:
    Job *parallelForImpl(int first, int last, ParallelForBody body, int chunk, bool frameScope);

    std::shared_ptr<State> state_;
    std::unique_ptr<ThreadPool> pool_;
    std::vector<Task *> poolTasks_;
    std::mutex lifecycleMu_;
};

}  // namespace thread
}  // namespace eve
