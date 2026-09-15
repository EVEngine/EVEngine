#pragma once

#include "animation/AnimPose.h"
#include "common/Result.h"

#include <string>
#include <memory>
#include <vector>
#include <span>
#include <cstddef>

namespace eve::animation {

class AnimClip;
class AnimSkeleton;
class MotionMatcher;
struct MotionFeatureLayout;
namespace detail { struct MotionSchemaState; }

/** @brief Copied clip-time interval contributing once to feature normalization; endpoints inclusive. */
struct MotionNormalizationRange {
    int clipIndex = 0;
    float start = 0.f;
    float end = 0.f;
};

/**
 * @brief Baked motion-matching feature database from one or more AnimClips.
 * Feature layout per frame:
 *   [0..1]   root velocity xz
 *   [2..7]   trajectory pos xz at +0.33/+0.66/+1.0s (character space)
 *   [8..9]   trajectory facing dir xz at +1.0s
 *   [10..]   selected joint world positions (xyz each)
 * The optional locomotion layout has 19 trajectory and 11 pose dimensions;
 * see setLocomotionFeatures(). Existing databases retain the basic layout.
 * Script type: `MotionDatabase`.
 */
class MotionDatabase {
public:
    explicit MotionDatabase(AnimSkeleton* skeleton);
    ~MotionDatabase();

    MotionDatabase(const MotionDatabase&)            = delete;
    MotionDatabase& operator=(const MotionDatabase&) = delete;

    AnimSkeleton* getSkeleton() const { return skeleton_; }

    /** @brief Include bone world position in pose features (by index). */
    void addFeatureBone(int boneIndex);
    void addFeatureBoneByName(const std::string& name);

    /** @brief Configure the locomotion layout before baking or creating matchers.
     * @param leftFoot Left foot bone index.
     * @param rightFoot Right foot bone index.
     * @param pelvis Pelvis bone index; local +Z is its heading axis (Y-up assets).
     * @return Applied, or InvalidArgument without changing the database.
     * @details Uses relative feet position, character-space feet velocities,
     * pelvis heading, and trajectory at -0.05, 0, 0.35, 0.7, 1 seconds.
     * Selects channel-wise mean-deviation normalization with a shared feet-velocity group.
     * Bone IDs must be distinct and valid. Reconfiguration after bake is rejected.
     * Borrowed skeleton/clips remain caller-owned and immutable during bake and playback.
     * @thread Owner thread; no callbacks, reentrancy or retained input pointers.
     */
    [[nodiscard]] eve::Result<void> setLocomotionFeatures(int leftFoot, int rightFoot, int pelvis);
    /** @brief Whether this database uses the 30-dimensional locomotion layout. */
    bool hasLocomotionFeatures() const { return locomotionFeatures_; }

    /** @brief Atomically configure an owning variable feature layout before bake.
     * @param layout Borrowed only for this call; all channels and strings are copied.
     * @return Applied, or InvalidArgument preserving the previous configuration.
     * @details Rejects invalid bones, axes, weights, times and unsupported pose
     * offsets. Skeleton and clips remain borrowed, immutable and caller-owned
     * throughout bake and matching. Configure before constructing any matcher.
     * Reconfiguration after bake is rejected. No callbacks or retained input pointers.
     * @thread Owner thread, outside bake/search; not reentrant.
     */
    [[nodiscard]] eve::Result<void> setFeatureLayout(const MotionFeatureLayout& layout);
    /** @brief Whether this database uses an explicitly configured variable layout. */
    bool hasFeatureLayout() const;
    /** @brief Atomically copy scalar source curves for the configured layout.
     * @param bytes Borrowed EVFC/1 bytes for this synchronous call only; decoded data is owned.
     * @param sources Borrowed source names in exact database clip order; names are copied.
     * @return Applied, or InvalidArgument preserving the previous curve data.
     * @details Call after adding all clips and configuring the layout, before bake.
     * Every clip must resolve to a source record; absent channels evaluate to zero.
     * Clips and skeleton remain caller-owned. No callbacks or retained input pointers.
     * @thread Owner thread, outside bake/search; not reentrant.
     */
    [[nodiscard]] eve::Result<void> setFeatureCurves(std::span<const std::byte> bytes,
                                                     std::span<const std::string> sources);
    /** @brief Atomically select weighted source ranges used to normalize a variable feature layout.
     * @param ranges Borrowed only for this call and copied in order. Duplicate and overlapping
     * ranges deliberately contribute again, matching multiple source database entries.
     * @return Number of sampled feature rows that will contribute, or InvalidArgument while
     * preserving the previous configuration.
     * @details Requires a configured unbaked variable layout and existing clips. Each finite
     * nonnegative inclusive interval must address a clip and contain at least one layout-rate
     * sample. Empty input, more than 100000 ranges or more than 100 million contributing rows
     * reject. Omit this call to normalize over every baked frame once. Inputs are owned after copy;
     * the borrowed clips and skeleton remain caller-owned and immutable through bake/matching.
     * @thread Owner thread before bake; synchronous, no callbacks or reentrancy.
     */
    [[nodiscard]] eve::Result<int> setFeatureNormalizationRanges(std::span<const MotionNormalizationRange> ranges);

    /**
     * @brief Bone used for trajectory / velocity features (default 0).
     * Mixamo clips typically want hips (`mixamorig:Hips`).
     */
    void setRootBone(int boneIndex);
    int  getRootBone() const { return rootBone_; }
    void setRootBoneByName(const std::string& name);

    void addClip(AnimClip* clip);
    int  getClipCount() const { return static_cast<int>(clips_.size()); }

    /** @brief Bake all clips into searchable frames. Call after addClip / feature bones.
     * @details Uses up to
     * eight native threads for 16 or more clips; frame and normalization
     * order match serial baking exactly.
     * Borrowed skeleton and clips must remain immutable
     * until this synchronous owner-thread call returns. No
     * callbacks or retained jobs.
     */
    void bake();
    bool isBaked() const { return baked_; }

    int getFrameCount() const { return static_cast<int>(frames_.size()); }
    int getFeatureSize() const { return featureSize_; }

    float     getFrameTime(int frameIndex) const;
    int       getFrameClipIndex(int frameIndex) const;
    AnimClip* getClip(int clipIndex) const;

    int getFeatureBoneCount() const { return static_cast<int>(featureBones_.size()); }
    int getFeatureBone(int index) const;

    /** @brief Copy feature vector into out[0..featureSize). */
    void getFeature(int frameIndex, float* out, int outCount) const;
    /** @brief Normalize a query with statistics computed by bake(). */
    void normalizeFeature(std::vector<float>& feature) const;

    struct Frame {
        int                clipIndex = 0;
        float              time      = 0.f;
        std::vector<float> feature;
        // Cached root for trajectory reconstruction helpers.
        float rootX = 0.f, rootZ = 0.f, rootYaw = 0.f;
        float velX = 0.f, velZ = 0.f;
        // Sum of unnormalized trajectory-velocity channel magnitudes, retained
        // before feature normalization for UE-compatible play-rate estimation.
        float trajectorySpeed = 0.f;
    };

    const Frame& frameAt(int index) const;

private:
    struct SchemaSampler;
    friend class MotionMatcher;
    float schemaTrajectorySpeed(std::span<const float> feature) const;
    void extractSchemaFeature(AnimClip* clip, float time, std::vector<float>& out, float& rootX,
                              float& rootZ, float& rootYaw, float& velX, float& velZ) const;
    void normalizeSchemaFeatures();
    void extractLocomotionFeature(AnimClip* clip, float time, std::vector<float>& out, float& rootX, float& rootZ,
                                  float& rootYaw, float& velX, float& velZ) const;
    void normalizeLocomotionFeatures();
    void requireBaked() const;
    void computeFeatureSize();
    void extractFeature(AnimClip* clip, float time, float dtSample, std::vector<float>& out, float& rootX, float& rootZ,
                        float& rootYaw, float& velX, float& velZ) const;
    static float yawFromQuat(float x, float y, float z, float w);

    AnimSkeleton*          skeleton_ = nullptr;
    std::vector<AnimClip*> clips_;
    std::vector<int>       featureBones_;
    std::vector<Frame>     frames_;
    // Contiguous baked-frame span for each clip; size is clips_.size() + 1.
    std::vector<int>       clipFrameOffsets_;
    int                    featureSize_ = 0;
    int                    rootBone_    = 0;
    bool                   baked_       = false;
    bool                   locomotionFeatures_ = false;
    std::vector<float>     featureMean_;
    std::vector<float>     featureInvStd_;
    mutable AnimPose       scratchPose_;
    std::unique_ptr<detail::MotionSchemaState> schema_;
    std::vector<MotionNormalizationRange> normalizationRanges_;
};

}  // namespace eve::animation
