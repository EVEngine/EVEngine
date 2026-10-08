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

    /** @brief Constructs a JobSystemThreadPool. */
    explicit JobSystemThreadPool(int workerCount);
    /** @brief Releases JobSystemThreadPool resources. */
    ~JobSystemThreadPool() override;

    JobSystemThreadPool(const JobSystemThreadPool &) = delete;
    JobSystemThreadPool &operator=(const JobSystemThreadPool &) = delete;

    /** @brief Returns the worker count. */
    int getWorkerCount() const override;
    /** @brief True when running. */
    bool isRunning() const override;
    /** @brief Returns the pending count. */
    int getPendingCount() const override;
    /** @brief Returns the outstanding count. */
    int getOutstandingCount() const override;

    /** @brief Submit. */
    Job *submit(JobFunc body) override;
    /** @brief Creates job. */
    Job *createJob(JobFunc body) override;
    /** @brief Schedule. */
    void schedule(Job *job) override;
    /** @brief Parallel for. */
    Job *parallelFor(int first, int last, ParallelForBody body, int chunk = 1) override;
    /** @brief Creates task group. */
    TaskGroup *createTaskGroup() override;

    /** @brief Submit frame. */
    Job *submitFrame(JobFunc body) override;
    /** @brief Creates frame job. */
    Job *createFrameJob(JobFunc body) override;
    /** @brief Parallel for frame. */
    Job *parallelForFrame(int first, int last, ParallelForBody body, int chunk = 1) override;
    /** @brief Creates frame task group. */
    TaskGroup *createFrameTaskGroup() override;
    /** @brief Begins frame. */
    void beginFrame() override;
    /** @brief Ends frame. */
    void endFrame() override;

    /** @brief Waits all. */
    void waitAll() override;
    /** @brief Stops . */
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
