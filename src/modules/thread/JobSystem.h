#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace eve {
namespace thread {

class JobSystem;

/**
 * @brief A unit of work executed by a JobSystem worker.
 */
using JobFunc = std::function<void()>;

/**
 * @brief Body of a parallel_for subrange.
 * @param first First index of the subrange (inclusive).
 * @param last  One past the last index of the subrange (exclusive).
 */
using ParallelForBody = std::function<void(int first, int last)>;

/**
 * @brief Handle to a single job inside a JobSystem.
 *
 * Every job carries a dependency count and an optional completion callback so
 * a render graph can be wired on top without rewriting the scheduler:
 * createJob() gives a paused job, addDependency() increments its dependency
 * count, schedule() kicks it, and setCompletionCallback() registers a callback
 * that fires on the finishing worker.
 *
 * Heap jobs are owned by the caller: delete them once they have finished
 * (isDone() or after wait()). Frame jobs are owned by the JobSystem arena and
 * must never be deleted; they stay valid until the next beginFrame()/endFrame().
 *
 * Thread-safety: a job may be waited on or deleted from any thread, and
 * dependencies may be added from any thread, but a job must be fully wired
 * (all addDependency calls) before it is scheduled.
 */
class Job {
public:
    virtual ~Job() = default;

    /**
     * @brief Block until this job finishes (done or failed).
     *
     * Safe to call from a JobSystem worker: the calling worker helps execute
     * ready jobs while it waits (fork/join pattern). Throws if a job waits on
     * itself.
     */
    virtual void wait() = 0;

    /** @brief Whether the job has finished (done or failed). */
    virtual bool isDone() const = 0;

    /** @brief Whether the job body (or completion callback) failed. */
    virtual bool hasFailed() const = 0;

    /**
     * @brief Error message from a failed body or completion callback.
     * @return Empty string when the job did not fail.
     */
    virtual std::string getError() const = 0;

    /**
     * @brief Number of not-yet-completed predecessors this job is waiting for.
     * @return Dependency count; 0 means the job is runnable once scheduled.
     */
    virtual int getPendingDependencyCount() const = 0;

    /**
     * @brief Make this job wait for another job to finish first.
     *
     * Must be called before schedule(); throws otherwise. A dependency on an
     * already-finished job is ignored. Both jobs must belong to the same
     * JobSystem.
     *
     * @param predecessor Job that must complete before this one runs.
     */
    virtual void addDependency(Job *predecessor) = 0;

    /**
     * @brief Register a callback fired once when the job finishes.
     *
     * Runs on the worker that executed the job, before any dependent job is
     * released, so it is safe for the callback to publish results that
     * downstream jobs consume. Exceptions thrown by the callback are caught and
     * recorded (hasFailed() stays false for the body's sake; getError() reports
     * the callback failure). If the job already finished, the callback runs
     * immediately on the calling thread.
     *
     * @param callback Callable invoked on completion (may be empty to clear).
     */
    virtual void setCompletionCallback(JobFunc callback) = 0;
};

/**
 * @brief Fork/join group: fork() spawns scheduled children, wait() joins them.
 *
 * wait() is safe from a worker thread (the worker helps execute ready jobs),
 * which makes task_group the natural building block for nested parallelism
 * inside another job. The group may be reused after wait(): children are
 * cleared once joined.
 */
class TaskGroup {
public:
    virtual ~TaskGroup() = default;

    /**
     * @brief Spawn a scheduled child job and add it to this group.
     * @param body Work to run on a worker.
     * @return Heap-allocated child job (caller owns it after wait()).
     */
    virtual Job *fork(JobFunc body) = 0;

    /**
     * @brief Spawn a scheduled child job with a completion callback.
     * @param body       Work to run on a worker.
     * @param onComplete Completion callback (see Job::setCompletionCallback).
     * @return Heap-allocated child job (caller owns it after wait()).
     */
    virtual Job *fork(JobFunc body, JobFunc onComplete) = 0;

    /**
     * @brief Join every forked child: block until all of them finish.
     * Safe from a worker thread.
     */
    virtual void wait() = 0;

    /** @brief Number of forked children that have not finished yet. */
    virtual int getPendingCount() const = 0;
};

/**
 * @brief Abstract job scheduler: dependencies, parallel_for, task_group, arena.
 *
 * This is the stable engine-wide API for CPU job scheduling. The default
 * backend (JobSystemThreadPool) schedules on top of the existing ThreadPool
 * worker pool; a TBB backend only needs to implement this interface, and the
 * factory in JobSystem.cpp is the single file that chooses the backend.
 *
 * Two allocation scopes exist:
 *  - Heap jobs (submit/createJob/parallelFor/createTaskGroup): caller-owned
 *    handles, deleted with `delete` once finished. Long-lived work (async
 *    scene loading) belongs here.
 *  - Frame jobs (submitFrame/createFrameJob/parallelForFrame/
 *    createFrameTaskGroup): allocated from a per-frame arena that is recycled
 *    by beginFrame()/endFrame(), so per-frame job submission does no
 *    new/delete. Handles are valid until the next beginFrame()/endFrame() and
 *    must not be deleted.
 */
class JobSystem {
public:
    virtual ~JobSystem() = default;

    /** @brief Number of scheduler worker threads. */
    virtual int getWorkerCount() const = 0;

    /** @brief Whether the system is still accepting new jobs. */
    virtual bool isRunning() const = 0;

    /** @brief Number of ready jobs waiting for a worker (approximate). */
    virtual int getPendingCount() const = 0;

    /** @brief Number of scheduled jobs that have not finished (approximate). */
    virtual int getOutstandingCount() const = 0;

    // ---- heap jobs (caller owns returned handles) ----

    /**
     * @brief Create a job and schedule it immediately.
     * @param body Work to run on a worker; may be empty to create a pure
     *             join/dependency node that just gates downstream jobs.
     * @return Heap-allocated Job; delete once finished.
     */
    virtual Job *submit(JobFunc body) = 0;

    /**
     * @brief Create a paused job (dependencies can be wired, then schedule()).
     * @param body Work to run on a worker; may be empty to create a pure
     *             join/dependency node that just gates downstream jobs.
     * @return Heap-allocated Job; delete once finished.
     */
    virtual Job *createJob(JobFunc body) = 0;

    /**
     * @brief Kick a paused job so it can run.
     *
     * A job with no pending dependencies becomes ready immediately; a job with
     * dependencies runs when its last predecessor finishes. Throws when the
     * system is stopped or the job was already scheduled.
     *
     * @param job Job previously returned by createJob()/createFrameJob().
     */
    virtual void schedule(Job *job) = 0;

    /**
     * @brief Run body over [first, last) in parallel.
     *
     * The range is statically split into ceil((last-first)/chunk) child jobs;
     * the returned job represents the whole loop and finishes when every child
     * is done. chunk <= 0 picks an automatic chunk targeting ~4 tasks per
     * worker.
     *
     * @param first First index (inclusive).
     * @param last  One past the last index (exclusive).
     * @param body  Called on a worker for each subrange [first, last).
     * @param chunk Subrange size per child task (default 1).
     * @return Heap-allocated loop Job; wait() on it joins the loop.
     */
    virtual Job *parallelFor(int first, int last, ParallelForBody body, int chunk = 1) = 0;

    /**
     * @brief Create a fork/join task group.
     * @return Heap-allocated TaskGroup; delete once joined.
     */
    virtual TaskGroup *createTaskGroup() = 0;

    // ---- frame jobs (arena-allocated; valid until next beginFrame) ----

    /**
     * @brief Create a frame-scoped job and schedule it immediately.
     * @param body Work to run on a worker.
     * @return Arena-allocated Job; do not delete, valid until next beginFrame().
     */
    virtual Job *submitFrame(JobFunc body) = 0;

    /**
     * @brief Create a paused frame-scoped job.
     * @param body Work to run on a worker.
     * @return Arena-allocated Job; do not delete, valid until next beginFrame().
     */
    virtual Job *createFrameJob(JobFunc body) = 0;

    /**
     * @brief Frame-scoped parallel_for; see parallelFor().
     * @return Arena-allocated loop Job; do not delete.
     */
    virtual Job *parallelForFrame(int first, int last, ParallelForBody body, int chunk = 1) = 0;

    /**
     * @brief Create a fork/join task group whose children are frame-scoped.
     * @return Arena-allocated TaskGroup; do not delete, valid until next beginFrame().
     */
    virtual TaskGroup *createFrameTaskGroup() = 0;

    /**
     * @brief Start a new frame: join leftover frame jobs and recycle the arena.
     *
     * All frame-scoped handles from the previous frame become invalid. Call
     * once per frame before submitting frame jobs.
     */
    virtual void beginFrame() = 0;

    /**
     * @brief End the frame: join outstanding frame jobs and recycle the arena.
     *
     * Blocks until every frame-scoped job finished, so results are safe to
     * consume (e.g. before swapping GPU buffers). Idempotent with beginFrame().
     */
    virtual void endFrame() = 0;

    // ---- lifecycle ----

    /**
     * @brief Block until every scheduled job has finished.
     * @throws eve::Exception when called from one of this system's workers.
     */
    virtual void waitAll() = 0;

    /**
     * @brief Stop accepting work and join workers. Idempotent.
     * @throws eve::Exception when called from one of this system's workers.
     */
    virtual void stop() = 0;
};

/**
 * @brief Create a JobSystem with the configured backend.
 *
 * This factory is the only place that knows which backend is compiled in;
 * swapping the scheduler (custom vs TBB) is a change to JobSystem.cpp alone.
 *
 * @param workerCount Worker thread count; <= 0 means hardware concurrency.
 * @return New JobSystem; the caller owns it and must delete it.
 */
JobSystem *createJobSystem(int workerCount = 0);

}  // namespace thread
}  // namespace eve
