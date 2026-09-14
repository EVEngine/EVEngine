#pragma once

/**
 * @file AsyncWork.h
 * @brief Capability for submitting CPU work onto a worker pool.
 *
 * ResourceManager lives in common/ and must not include the thread module.
 * The thread module registers `IAsyncWorkExecutor` at construction; when the
 * capability is absent (trimmed builds, unit tests), ResourceManager runs
 * `request()` inline on the calling thread.
 */

#include "common/Export.h"
#include "common/Result.h"

#include <functional>

namespace eve::caps {

/**
 * @brief Fire-and-forget CPU job submission used by the resource cache.
 *
 * Workers must not touch the Squirrel VM, Vulkan device, or Resource::adopt.
 * They may call `IAssetReloader::load()` and insert into ResourceManager.
 */
class EVENGINE_API IAsyncWorkExecutor {
public:
    static constexpr const char *capabilityName = "IAsyncWorkExecutor";
    virtual ~IAsyncWorkExecutor() = default;

    /**
     * @brief Queue `work` for a worker thread (or run it inline).
     * @param work Callable that owns all data it needs; invoked at most once.
     * @return `Applied` when queued or executed; `Rejected`/`Failed` when the
     *         pool cannot accept work. A successful result does not mean `work`
     *         has finished.
     * @ownership `work` is taken by the executor for the lifetime of the job.
     * @lifetime The callable must not capture pointers that die before the job
     *           completes; ResourceManager keeps cache/pending state alive.
     * @thread Safe to call from the game thread; the executor may invoke `work`
     *         on a worker or, when no pool exists, on the caller.
     * @reentrancy `work` must not call `submit()` on the same executor in a way
     *             that requires the submitting thread to drain the pool.
     */
    [[nodiscard("async submit outcome must be checked")]] virtual eve::Result<void> submit(
        std::function<void()> work) = 0;
};

}  // namespace eve::caps
