#pragma once
#include "common/Export.h"

#include <vector>

namespace eve::animation {

class AnimClip;
class AnimPose;
class AnimSkeleton;

/** @brief Parallel batch evaluator for independent character clip poses. Script type: `AnimBatch`. */
class EVENGINE_API_WORLD AnimBatch {
public:
    /** @brief Anim batch. */
    AnimBatch() = default;

    /** @brief Queue one clip sample. The clip, skeleton and pose must outlive evaluate(). */
    void add(AnimClip* clip, AnimSkeleton* skeleton, AnimPose* pose, float time, int lodLevel = 0);
    /** @brief Remove all queued jobs without deleting referenced objects. */
    void clear() { jobs_.clear(); }
    /** @brief Returns the count. */
    int getCount() const { return static_cast<int>(jobs_.size()); }
    /** @brief Evaluate queued poses, using hardware concurrency when workerCount is zero. */
    void evaluate(int workerCount = 0);
    /** @brief Returns the last worker count. */
    int getLastWorkerCount() const { return lastWorkerCount_; }

private:
    struct Job {
        AnimClip* clip = nullptr;
        AnimSkeleton* skeleton = nullptr;
        AnimPose* pose = nullptr;
        float time = 0.f;
        int lodLevel = 0;
    };
    std::vector<Job> jobs_;
    int lastWorkerCount_ = 0;
};

}  // namespace eve::animation
