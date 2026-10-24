#pragma once
#include <functional>
#include "common/Result.h"

namespace eve::crowd {
/** @brief Explicit execution selection; serial does not require a provider. */
enum class CrowdExecution { Serial, Parallel };
/** @brief Synchronous scheduler for fixed logical partitions, independent of worker identity.
 * @ownership Caller retains shared adapter lease; callbacks are borrowed only until execute returns.
 * @thread Invoked by the Crowd owner; bodies may run concurrently on workers.
 * @reentrancy Bodies must not reenter or mutate Crowd through public APIs. No callbacks under a lock.
 */
class CrowdWorkScheduler {
public:
    /** @brief Release the adapter after every synchronous dispatch has joined. */
    virtual ~CrowdWorkScheduler() = default;
    /** @brief Execute each partition exactly once, joining every dispatched body on success or failure.
     * @param partitions Nonnegative partition count.
     * @param body Borrowed native callback; each invocation owns its partition's output slots.
     * @return Failure after join; never retry or silently execute serial work.
     * @ownership No callback or data reference survives this call.
     * @thread Owner dispatch, concurrent worker callbacks. @reentrancy No Crowd reentry.
     * @cost Linear scheduling work per partition; amortize with fixed 256-item logical partitions.
     */
    [[nodiscard]] virtual Result<void> execute(int partitions, const std::function<void(int)>& body) = 0;
};
}  // namespace eve::crowd
