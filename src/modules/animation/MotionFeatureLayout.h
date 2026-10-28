#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace eve::animation {
/** @brief Vector or scalar operation encoded by a motion-search channel. */
enum class MotionFeatureKind : std::uint8_t { Position, Velocity, Heading, Curve };
/** @brief Source of a channel's samples. */
enum class MotionFeatureSource : std::uint8_t { Pose, Trajectory };
/** @brief Query data policy while a valid continuing pose exists. */
enum class MotionFeatureQuery : std::uint8_t { Character, Continuing };

/** @brief Owning configuration for one ordered motion feature channel.
 * @details Bone IDs belong to the database skeleton. Trajectory channels sample
 * the configured root; pose channels sample bone relative to origin. Bit 0/1/2
 * in axes includes X/Y/Z, in that order. Values use metres and seconds. Velocity
 * normalization clamps the full vector at 0.01 m/s before component stripping.
 * Empty normalizationGroup keeps this channel independent; equal nonempty groups
 * pool channels with the same operation and cardinality. Query policy applies to
 * pose channels; trajectories always use character prediction. Curve channels
 * use source Pose, axes=1 and a nonempty curve name, with scalar values and an
 * arbitrary sample offset in the supported range. Vector channels leave curve empty.
 */
struct MotionFeatureChannel {
    MotionFeatureKind kind = MotionFeatureKind::Position;
    MotionFeatureSource source = MotionFeatureSource::Pose;
    MotionFeatureQuery query = MotionFeatureQuery::Continuing;
    int bone = 0, origin = 0;
    std::uint8_t axes = 7;
    int headingAxis = 0;
    float sampleTime = 0.f;
    float weight = 1.f;
    bool characterSpaceVelocity = true;
    bool normalizeVelocity = false;
    std::string normalizationGroup;
    std::string curve;
};

/** @brief Owning ordered feature layout; copied atomically by a database.
 * @details Configuration is owner-thread only, with no callbacks or external
 * references. Sample rate is 1..240 Hz. Up to 256 channels and 1024 dimensions.
 * This runtime value is not a serialized format. Vector pose offsets require
 * zero; trajectory and scalar curve offsets range from -10 to +10 seconds.
 */
struct MotionFeatureLayout {
    int sampleRate = 30;
    std::vector<MotionFeatureChannel> channels;
    /** @brief Scale applied to metre-based position/velocity features before pooling;
     * dimensionless headings, curves and normalized velocities are unchanged.
     * Use 100 for source configurations authored in centimetres; default is metres.
     */
    float normalizationLengthScale = 1.f;
};

/** @brief Copied trajectory sample at an explicit offset from the current frame.
 * @details Position is world-axis displacement from the current root in metres;
 * velocity is world metres/second, yaw is world radians. Queries provide one
 * sample for every distinct trajectory time required by their layout.
 */
struct MotionFeatureTrajectorySample {
    float seconds = 0.f;
    float x = 0.f, y = 0.f, z = 0.f;
    float vx = 0.f, vy = 0.f, vz = 0.f;
    float yaw = 0.f;
};

/** @brief Copied evaluated scalar curve query at a schema-relative time.
 * @details A missing authored curve is supplied explicitly as zero. Values are
 * scalars, not angles; negative values retain their sign. No references retained.
 */
struct MotionFeatureCurveSample {
    std::string curve;
    float seconds = 0.f;
    float value = 0.f;
};
}  // namespace eve::animation
