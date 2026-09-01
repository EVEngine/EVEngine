#include "pixelworld_thread/PixelWorldThread.h"

#include "thread/JobSystem.h"

#include <algorithm>
#include <memory>

namespace eve::pixelworld_thread {

JobSystemPixelScheduler::JobSystemPixelScheduler(eve::thread::JobSystem& jobs) noexcept
    : jobs_(&jobs) {}

void JobSystemPixelScheduler::parallelFor(
    std::size_t workItems, const std::function<void(std::size_t)>& body) {
    if (workItems == 0) return;
    try {
        std::unique_ptr<eve::thread::Job> job(jobs_->parallelFor(
            0, int(workItems), [&body](int first, int last) {
                for (int index = first; index < last; ++index) body(std::size_t(index));
            }, 1));
        job->wait();
        if (!job->hasFailed()) return;
    } catch (...) {
        // The serial pass below restores every index-owned result slot.
    }
    for (std::size_t index = 0; index < workItems; ++index) body(index);
}

std::size_t JobSystemPixelScheduler::workerCount() const noexcept {
    return std::size_t(std::max(0, jobs_->getWorkerCount()));
}

}  // namespace eve::pixelworld_thread
