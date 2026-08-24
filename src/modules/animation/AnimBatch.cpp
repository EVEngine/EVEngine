#include "animation/AnimBatch.h"

#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "common/Exception.h"

#include <algorithm>
#include <atomic>
#include <thread>

namespace eve::animation {

void AnimBatch::add(AnimClip* clip, AnimSkeleton* skeleton, AnimPose* pose, float time, int lodLevel) {
    if (!clip || !skeleton || !pose) throw Exception("AnimBatch.add: clip, skeleton and pose are required");
    if (lodLevel < 0) throw Exception("AnimBatch.add: lodLevel must be >= 0");
    for (const Job& job : jobs_) if (job.pose == pose) throw Exception("AnimBatch.add: pose is already queued");
    jobs_.push_back({clip, skeleton, pose, time, lodLevel});
}

void AnimBatch::evaluate(int workerCount) {
    if (workerCount < 0) throw Exception("AnimBatch.evaluate: workerCount must be >= 0");
    if (jobs_.empty()) { lastWorkerCount_ = 0; return; }
    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    const size_t count = std::min(jobs_.size(), static_cast<size_t>(workerCount > 0 ? workerCount : hardware));
    lastWorkerCount_ = static_cast<int>(count);
    if (count == 1) {
        for (Job& job : jobs_) job.clip->sampleLod(job.time, job.pose, job.skeleton, job.lodLevel);
        return;
    }
    std::atomic<size_t> next{0};
    std::vector<std::thread> workers;
    workers.reserve(count);
    for (size_t worker = 0; worker < count; ++worker) {
        workers.emplace_back([&] {
            for (;;) {
                const size_t index = next.fetch_add(1, std::memory_order_relaxed);
                if (index >= jobs_.size()) break;
                Job& job = jobs_[index];
                job.clip->sampleLod(job.time, job.pose, job.skeleton, job.lodLevel);
            }
        });
    }
    for (std::thread& worker : workers) worker.join();
}

}  // namespace eve::animation
