#pragma once
#include <memory>
#include "common/Export.h"
#include "common/Result.h"
namespace eve::thread {
class JobSystem;
}
namespace eve::crowd {
class CrowdWorkScheduler;
}
namespace eve::crowd_thread {
/** @brief Create a synchronous Crowd scheduler observing a genuinely shared JobSystem owner.
 * @param jobs Weak reference to shared ownership of the owned createJobSystem result, never a no-op borrowed deleter.
 * @return Owned shared scheduler adapter; expired providers fail each dispatch explicitly.
 * @ownership The adapter does not retain the pool while idle. Dispatch locks its owner through submit and join.
 * @thread Creation on composition root; dispatch on Crowd owner. Do not stop the pool during dispatch.
 * @reentrancy Native bodies may execute concurrently, without adapter locks; no Crowd reentry.
 * @cost One adapter allocation per composition; reuse its lease across simulation ticks.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<std::shared_ptr<eve::crowd::CrowdWorkScheduler>> makeJobSystemCrowdScheduler(
    std::weak_ptr<eve::thread::JobSystem> jobs);
}  // namespace eve::crowd_thread
