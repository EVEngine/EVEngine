#include "thread/JobSystemThreadPool.h"

#include "common/Exception.h"
#include "thread/ThreadPool.h"

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace eve {
namespace thread {

namespace {

enum class JobStatus { Pending, Scheduled, Running, Done, Failed };
enum class JobScope { Heap, Frame };

struct JobImpl;

thread_local const void *tlsCurrentState = nullptr;
thread_local JobImpl *tlsCurrentJob = nullptr;

bool isDoneStatus(JobStatus status) {
    return status == JobStatus::Done || status == JobStatus::Failed;
}

}  // namespace

namespace {

/**
 * @brief Simple bump allocator for frame-scoped jobs.
 * Grows in fixed-size blocks; reset() destroys every tracked object and
 * recycles the blocks, so per-frame job submission never calls new/delete.
 */
class FrameArena {
public:
    FrameArena() = default;
    ~FrameArena() { reset(); }

    /**
     * @brief Allocate aligned memory from the current block.
     * @param size  Object size in bytes.
     * @param align Alignment (power of two).
     * @return Raw memory; the caller placement-news into it and must track it.
     */
    void *alloc(size_t size, size_t align) {
        if (size > kArenaBlockSize)
            throw eve::Exception("JobSystem arena allocation exceeds block size");
        if (blocks_.empty())
            blocks_.emplace_back(kArenaBlockSize);
        size_t aligned = (offset_ + align - 1) & ~(align - 1);
        if (aligned + size > blocks_.back().size()) {
            blocks_.emplace_back(kArenaBlockSize);
            aligned = 0;
        }
        void *ptr = blocks_.back().data() + aligned;
        offset_ = aligned + size;
        return ptr;
    }

    /**
     * @brief Register a constructed object for destruction at reset().
     * @param ptr     Pointer to the constructed object.
     * @param destroy Function that calls its destructor.
     */
    void track(void *ptr, void (*destroy)(void *)) {
        tracked_.push_back({ptr, destroy});
    }

    /** @brief Destroy every tracked object and recycle the blocks. */
    void reset() {
        for (auto &entry : tracked_)
            entry.destroy(entry.ptr);
        tracked_.clear();
        offset_ = 0;
    }

private:
    static constexpr size_t kArenaBlockSize = 64 * 1024;

    struct Tracked {
        void *ptr;
        void (*destroy)(void *);
    };

    std::vector<std::vector<std::byte>> blocks_;
    std::vector<Tracked> tracked_;
    size_t offset_ = 0;
};

}  // namespace

struct JobSystemThreadPool::State {
    mutable std::mutex mu;
    std::condition_variable cv;
    std::deque<JobImpl *> ready;
    int outstanding = 0;
    int outstandingFrame = 0;
    // Number of jobs currently between status=Done/Failed and the tail of
    // completeJob (fireCompletion / releaseDependents / counter decrement).
    // waitFrameJobs() must not reset the frame arena while this is > 0:
    // a child releases its join before its own counters are decremented, so
    // another thread can finish the join, drive outstandingFrame to 0 and
    // reset the arena while this worker is still touching the child (a
    // use-after-free that shows up as a null std::function call in
    // fireCompletion under concurrent GPU work).
    int completing = 0;
    int workerCount = 0;
    bool stopping = false;
    FrameArena arena;

    JobImpl *createJobLocked(JobScope scope, JobFunc body, JobFunc onComplete);
    void scheduleLocked(JobImpl *job);
    void enqueueLocked(JobImpl *job);
    void runJob(JobImpl *job);
    void completeJob(JobImpl *job, bool ok);
    void fireCompletion(JobImpl *job);
    void recordError(JobImpl *job, const std::string &message);
    void releaseDependents(JobImpl *job);
    void waitJob(JobImpl *job);
    void waitFrameJobs();
    int autoChunk(int count) const;

    static void workerLoop(State *state);
};

namespace {

struct JobImpl final : public Job {
    JobImpl(JobSystemThreadPool::State *owner, JobScope scope_, JobFunc fn, JobFunc onDone)
        : state(owner), scope(scope_), body(std::move(fn)), onComplete(std::move(onDone)) {}

    ~JobImpl() override {
        // parallel_for children are owned by their join job and are guaranteed
        // complete before the join finishes, so deleting them here is safe.
        // Frame-scope jobs never populate ownedChildren (the arena owns them).
        for (JobImpl *child : ownedChildren)
            delete child;
    }

    void wait() override;
    bool isDone() const override;
    bool hasFailed() const override;
    std::string getError() const override;
    int getPendingDependencyCount() const override;
    void addDependency(Job *predecessor) override;
    void setCompletionCallback(JobFunc callback) override;

    JobSystemThreadPool::State *state;
    JobScope scope;
    JobFunc body;
    JobFunc onComplete;
    int depCount = 0;
    std::vector<JobImpl *> dependents;
    std::vector<JobImpl *> ownedChildren;
    JobStatus status = JobStatus::Pending;
    bool scheduled = false;
    bool enqueued = false;
    // Set under mu at the very end of completeJob. waitJob() waits on this
    // (not just status) so a caller that deletes a join job never frees a
    // child while a worker is still in the child's completeJob tail.
    bool completionDone = false;
    std::string error;
};

class TaskGroupImpl final : public TaskGroup {
public:
    TaskGroupImpl(JobSystemThreadPool::State *owner, JobScope scope_)
        : state(owner), scope(scope_) {}
    ~TaskGroupImpl() override = default;

    Job *fork(JobFunc body) override { return fork(std::move(body), JobFunc{}); }

    Job *fork(JobFunc body, JobFunc onComplete) override {
        if (!body)
            throw eve::Exception("TaskGroup::fork: null function");
        JobImpl *job = nullptr;
        {
            std::lock_guard<std::mutex> lock(state->mu);
            if (state->stopping)
                throw eve::Exception("JobSystem is stopped");
            job = state->createJobLocked(scope, std::move(body), std::move(onComplete));
            state->scheduleLocked(job);
            children.push_back(job);
        }
        state->cv.notify_one();
        return job;
    }

    void wait() override {
        std::vector<JobImpl *> snapshot;
        {
            std::lock_guard<std::mutex> lock(state->mu);
            snapshot = children;
        }
        for (JobImpl *child : snapshot)
            state->waitJob(child);
        std::lock_guard<std::mutex> lock(state->mu);
        children.clear();
    }

    int getPendingCount() const override {
        std::lock_guard<std::mutex> lock(state->mu);
        int pending = 0;
        for (JobImpl *child : children) {
            if (!isDoneStatus(child->status))
                ++pending;
        }
        return pending;
    }

    JobSystemThreadPool::State *state;
    JobScope scope;
    std::vector<JobImpl *> children;
};

}  // namespace

// ---- State ----

JobImpl *JobSystemThreadPool::State::createJobLocked(JobScope scope, JobFunc body,
                                                     JobFunc onComplete) {
    if (scope == JobScope::Frame) {
        void *mem = arena.alloc(sizeof(JobImpl), alignof(JobImpl));
        auto *job = new (mem) JobImpl(this, scope, std::move(body), std::move(onComplete));
        arena.track(job, [](void *ptr) { static_cast<JobImpl *>(ptr)->~JobImpl(); });
        return job;
    }
    return new JobImpl(this, scope, std::move(body), std::move(onComplete));
}

void JobSystemThreadPool::State::scheduleLocked(JobImpl *job) {
    job->scheduled = true;
    ++outstanding;
    if (job->scope == JobScope::Frame)
        ++outstandingFrame;
    if (job->depCount == 0)
        enqueueLocked(job);
}

void JobSystemThreadPool::State::enqueueLocked(JobImpl *job) {
    if (job->enqueued)
        return;
    job->enqueued = true;
    ready.push_back(job);
}

void JobSystemThreadPool::State::runJob(JobImpl *job) {
    {
        std::lock_guard<std::mutex> lock(mu);
        job->status = JobStatus::Running;
    }

    // A job body that calls waitAll()/stop() must be treated as a worker even
    // when it is help-executed inline by a waiting thread (fork/join path),
    // otherwise those calls would re-enter the wait they are running inside.
    const void *previousState = tlsCurrentState;
    if (previousState != this)
        tlsCurrentState = this;

    JobImpl *previous = tlsCurrentJob;
    tlsCurrentJob = job;
    bool ok = true;
    if (job->body) {
        try {
            job->body();
        } catch (const eve::Exception &e) {
            ok = false;
            recordError(job, e.what());
        } catch (const std::exception &e) {
            ok = false;
            recordError(job, e.what());
        } catch (...) {
            ok = false;
            recordError(job, "unknown exception");
        }
    }
    tlsCurrentJob = previous;

    completeJob(job, ok);
    tlsCurrentState = previousState;
}

void JobSystemThreadPool::State::completeJob(JobImpl *job, bool ok) {
    {
        std::lock_guard<std::mutex> lock(mu);
        job->status = ok ? JobStatus::Done : JobStatus::Failed;
        ++completing;
    }
    cv.notify_all();

    // Completion callback runs before dependents are released so it can
    // publish results downstream jobs consume.
    fireCompletion(job);

    // Decrement the outstanding counters while the job is still pinned by
    // `completing` (waitFrameJobs waits on it too), release dependents, then
    // mark the job's own memory as finished. All of this stays inside one
    // critical section: a waiter can only return once completionDone is set,
    // so it cannot delete the job (heap jobs are owned by the caller) while
    // this worker is still iterating job->dependents. The dependents are
    // released before completionDone so a join can never become runnable (and
    // later be deleted) while this job is still writing its own fields.
    {
        std::lock_guard<std::mutex> lock(mu);
        --outstanding;
        if (job->scope == JobScope::Frame)
            --outstandingFrame;
        releaseDependents(job);
        job->completionDone = true;
    }
    {
        std::lock_guard<std::mutex> lock(mu);
        --completing;
    }
    cv.notify_all();
}

void JobSystemThreadPool::State::fireCompletion(JobImpl *job) {
    if (!job->onComplete)
        return;
    try {
        job->onComplete();
    } catch (const eve::Exception &e) {
        recordError(job, std::string("completion callback: ") + e.what());
    } catch (const std::exception &e) {
        recordError(job, std::string("completion callback: ") + e.what());
    } catch (...) {
        recordError(job, "completion callback: unknown exception");
    }
}

void JobSystemThreadPool::State::recordError(JobImpl *job, const std::string &message) {
    std::lock_guard<std::mutex> lock(mu);
    if (!job->error.empty())
        job->error += "; ";
    job->error += message;
}

void JobSystemThreadPool::State::releaseDependents(JobImpl *job) {
    // Caller holds mu. (Kept as a helper so completeJob reads as a single
    // critical section; the enqueue + depCount updates below are lock-free
    // only because they are performed under that same lock.)
    bool woke = false;
    for (JobImpl *dep : job->dependents) {
        if (dep->depCount > 0)
            --dep->depCount;
        if (dep->depCount == 0 && dep->scheduled && !dep->enqueued) {
            enqueueLocked(dep);
            woke = true;
        }
    }
    job->dependents.clear();
    if (woke)
        cv.notify_all();
}

void JobSystemThreadPool::State::waitJob(JobImpl *job) {
    std::unique_lock<std::mutex> lock(mu);
    if (job->completionDone)
        return;
    if (tlsCurrentJob == job)
        throw eve::Exception("JobSystem: a job cannot wait on itself");

    for (;;) {
        if (job->completionDone)
            return;
        if (!ready.empty()) {
            JobImpl *next = ready.front();
            ready.pop_front();
            lock.unlock();
            runJob(next);
            lock.lock();
            continue;
        }
        if (stopping)
            return;
        cv.wait(lock);
    }
}

void JobSystemThreadPool::State::waitFrameJobs() {
    std::unique_lock<std::mutex> lock(mu);
    while (outstandingFrame > 0 || completing > 0) {
        if (!ready.empty()) {
            JobImpl *next = ready.front();
            ready.pop_front();
            lock.unlock();
            runJob(next);
            lock.lock();
            continue;
        }
        if (stopping)
            return;
        cv.wait(lock);
    }
    arena.reset();
}

int JobSystemThreadPool::State::autoChunk(int count) const {
    const int target = std::max(1, workerCount * 4);
    return std::max(1, (count + target - 1) / target);
}

void JobSystemThreadPool::State::workerLoop(State *state) {
    tlsCurrentState = state;
    for (;;) {
        JobImpl *job = nullptr;
        {
            std::unique_lock<std::mutex> lock(state->mu);
            state->cv.wait(lock, [state] { return state->stopping || !state->ready.empty(); });
            if (state->stopping && state->ready.empty())
                break;
            job = state->ready.front();
            state->ready.pop_front();
        }
        if (job)
            state->runJob(job);
    }
    tlsCurrentState = nullptr;
}

// ---- JobImpl ----

void JobImpl::wait() {
    state->waitJob(this);
}

bool JobImpl::isDone() const {
    std::lock_guard<std::mutex> lock(state->mu);
    return completionDone;
}

bool JobImpl::hasFailed() const {
    std::lock_guard<std::mutex> lock(state->mu);
    return status == JobStatus::Failed;
}

std::string JobImpl::getError() const {
    std::lock_guard<std::mutex> lock(state->mu);
    return error;
}

int JobImpl::getPendingDependencyCount() const {
    std::lock_guard<std::mutex> lock(state->mu);
    return depCount;
}

void JobImpl::addDependency(Job *predecessor) {
    auto *pred = dynamic_cast<JobImpl *>(predecessor);
    if (!pred || pred->state != state)
        throw eve::Exception("JobSystem::addDependency: job does not belong to this system");
    if (pred == this)
        throw eve::Exception("JobSystem::addDependency: a job cannot depend on itself");

    std::lock_guard<std::mutex> lock(state->mu);
    if (scheduled)
        throw eve::Exception("JobSystem::addDependency: job already scheduled");
    if (isDoneStatus(pred->status))
        return;  // Predecessor already finished; nothing to wait for.
    ++depCount;
    pred->dependents.push_back(this);
}

void JobImpl::setCompletionCallback(JobFunc callback) {
    bool alreadyDone = false;
    {
        std::lock_guard<std::mutex> lock(state->mu);
        if (isDoneStatus(status)) {
            alreadyDone = true;
        } else {
            onComplete = std::move(callback);
            return;
        }
    }
    if (alreadyDone && callback) {
        try {
            callback();
        } catch (const eve::Exception &e) {
            state->recordError(this, std::string("completion callback: ") + e.what());
        } catch (const std::exception &e) {
            state->recordError(this, std::string("completion callback: ") + e.what());
        } catch (...) {
            state->recordError(this, "completion callback: unknown exception");
        }
    }
}

// ---- JobSystemThreadPool ----

namespace {

JobImpl *castJob(JobSystemThreadPool::State *state, Job *job) {
    if (!job)
        throw eve::Exception("JobSystem: null job");
    auto *impl = dynamic_cast<JobImpl *>(job);
    if (!impl || impl->state != state)
        throw eve::Exception("JobSystem: job does not belong to this system");
    return impl;
}

}  // namespace

JobSystemThreadPool::JobSystemThreadPool(int workerCount) {
    if (workerCount <= 0) {
        workerCount = static_cast<int>(std::thread::hardware_concurrency());
        if (workerCount <= 0)
            workerCount = 1;
    }
    state_ = std::make_shared<State>();
    pool_ = std::make_unique<ThreadPool>(workerCount);
    state_->workerCount = pool_->getWorkerCount();
    poolTasks_.reserve(static_cast<size_t>(state_->workerCount));
    try {
        for (int i = 0; i < state_->workerCount; ++i) {
            auto state = state_;
            poolTasks_.push_back(pool_->submit([state] { State::workerLoop(state.get()); }));
        }
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(state_->mu);
            state_->stopping = true;
        }
        state_->cv.notify_all();
        if (pool_)
            pool_->stop();
        for (Task *task : poolTasks_)
            delete task;
        poolTasks_.clear();
        throw;
    }
}

JobSystemThreadPool::~JobSystemThreadPool() {
    try {
        std::lock_guard<std::mutex> lifecycleLock(lifecycleMu_);
        const bool fromWorker = tlsCurrentState == state_.get();
        {
            std::lock_guard<std::mutex> lock(state_->mu);
            state_->stopping = true;
        }
        state_->cv.notify_all();

        if (fromWorker) {
            // Destroyed from inside one of our jobs: joining any worker could
            // deadlock (another worker may wait on this task). Mirror
            // ThreadPool's worker-caller path — detach; worker loops only
            // touch shared State, which outlives them through their capture.
            for (Task *task : poolTasks_)
                delete task;
            poolTasks_.clear();
            pool_.reset();
            return;
        }
        if (pool_)
            pool_->stop();
        for (Task *task : poolTasks_)
            delete task;
        poolTasks_.clear();
    } catch (...) {
    }
}

int JobSystemThreadPool::getWorkerCount() const {
    return state_->workerCount;
}

bool JobSystemThreadPool::isRunning() const {
    std::lock_guard<std::mutex> lock(state_->mu);
    return !state_->stopping;
}

int JobSystemThreadPool::getPendingCount() const {
    std::lock_guard<std::mutex> lock(state_->mu);
    return static_cast<int>(state_->ready.size());
}

int JobSystemThreadPool::getOutstandingCount() const {
    std::lock_guard<std::mutex> lock(state_->mu);
    return state_->outstanding;
}

Job *JobSystemThreadPool::submit(JobFunc body) {
    JobImpl *job = nullptr;
    {
        std::lock_guard<std::mutex> lock(state_->mu);
        if (state_->stopping)
            throw eve::Exception("JobSystem is stopped");
        job = state_->createJobLocked(JobScope::Heap, std::move(body), JobFunc{});
        state_->scheduleLocked(job);
    }
    state_->cv.notify_one();
    return job;
}

Job *JobSystemThreadPool::createJob(JobFunc body) {
    std::lock_guard<std::mutex> lock(state_->mu);
    return state_->createJobLocked(JobScope::Heap, std::move(body), JobFunc{});
}

void JobSystemThreadPool::schedule(Job *job) {
    JobImpl *impl = castJob(state_.get(), job);
    {
        std::lock_guard<std::mutex> lock(state_->mu);
        if (state_->stopping)
            throw eve::Exception("JobSystem is stopped");
        if (impl->scheduled)
            throw eve::Exception("JobSystem::schedule: job already scheduled");
        state_->scheduleLocked(impl);
    }
    state_->cv.notify_one();
}

Job *JobSystemThreadPool::parallelFor(int first, int last, ParallelForBody body, int chunk) {
    return parallelForImpl(first, last, std::move(body), chunk, false);
}

TaskGroup *JobSystemThreadPool::createTaskGroup() {
    return new TaskGroupImpl(state_.get(), JobScope::Heap);
}

Job *JobSystemThreadPool::submitFrame(JobFunc body) {
    JobImpl *job = nullptr;
    {
        std::lock_guard<std::mutex> lock(state_->mu);
        if (state_->stopping)
            throw eve::Exception("JobSystem is stopped");
        job = state_->createJobLocked(JobScope::Frame, std::move(body), JobFunc{});
        state_->scheduleLocked(job);
    }
    state_->cv.notify_one();
    return job;
}

Job *JobSystemThreadPool::createFrameJob(JobFunc body) {
    std::lock_guard<std::mutex> lock(state_->mu);
    return state_->createJobLocked(JobScope::Frame, std::move(body), JobFunc{});
}

Job *JobSystemThreadPool::parallelForFrame(int first, int last, ParallelForBody body, int chunk) {
    return parallelForImpl(first, last, std::move(body), chunk, true);
}

TaskGroup *JobSystemThreadPool::createFrameTaskGroup() {
    std::lock_guard<std::mutex> lock(state_->mu);
    void *mem = state_->arena.alloc(sizeof(TaskGroupImpl), alignof(TaskGroupImpl));
    auto *group = new (mem) TaskGroupImpl(state_.get(), JobScope::Frame);
    state_->arena.track(group, [](void *ptr) { static_cast<TaskGroupImpl *>(ptr)->~TaskGroupImpl(); });
    return group;
}

void JobSystemThreadPool::beginFrame() {
    state_->waitFrameJobs();
}

void JobSystemThreadPool::endFrame() {
    state_->waitFrameJobs();
}

void JobSystemThreadPool::waitAll() {
    if (tlsCurrentState == state_.get())
        throw eve::Exception("JobSystem::waitAll cannot be called from its worker");
    State *state = state_.get();
    std::unique_lock<std::mutex> lock(state->mu);
    while (state->outstanding > 0) {
        if (!state->ready.empty()) {
            JobImpl *next = state->ready.front();
            state->ready.pop_front();
            lock.unlock();
            state->runJob(next);
            lock.lock();
            continue;
        }
        if (state->stopping)
            return;
        state->cv.wait(lock);
    }
}

void JobSystemThreadPool::stop() {
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMu_);
    if (tlsCurrentState == state_.get())
        throw eve::Exception("JobSystem::stop cannot be called from its worker");
    {
        std::lock_guard<std::mutex> lock(state_->mu);
        state_->stopping = true;
    }
    state_->cv.notify_all();
    if (pool_)
        pool_->stop();
    for (Task *task : poolTasks_)
        delete task;
    poolTasks_.clear();
}

Job *JobSystemThreadPool::parallelForImpl(int first, int last, ParallelForBody body, int chunk,
                                          bool frameScope) {
    if (!body)
        throw eve::Exception("JobSystem::parallelFor: null function");
    if (last <= first)
        return frameScope ? submitFrame(JobFunc{}) : submit(JobFunc{});

    const JobScope scope = frameScope ? JobScope::Frame : JobScope::Heap;
    const int count = last - first;
    if (chunk <= 0)
        chunk = state_->autoChunk(count);
    const int taskCount = (count + chunk - 1) / chunk;

    std::lock_guard<std::mutex> lock(state_->mu);
    if (state_->stopping)
        throw eve::Exception("JobSystem is stopped");

    JobImpl *join = state_->createJobLocked(scope, JobFunc{}, JobFunc{});
    join->depCount = taskCount;
    if (scope == JobScope::Heap)
        join->ownedChildren.reserve(static_cast<size_t>(taskCount));

    std::vector<JobImpl *> children;
    children.reserve(static_cast<size_t>(taskCount));
    for (int t = 0; t < taskCount; ++t) {
        const int begin = first + t * chunk;
        const int end = std::min(last, begin + chunk);
        JobImpl *child =
            state_->createJobLocked(scope, [body, begin, end] { body(begin, end); }, JobFunc{});
        child->dependents.push_back(join);
        children.push_back(child);
        if (scope == JobScope::Heap)
            join->ownedChildren.push_back(child);
    }

    // Schedule the join first so it sits waiting, then the children; the join
    // becomes ready when the last child completes.
    state_->scheduleLocked(join);
    for (JobImpl *child : children)
        state_->scheduleLocked(child);
    state_->cv.notify_all();
    return join;
}

}  // namespace thread
}  // namespace eve
