#pragma once

#include "animation/AnimMath.h"
#include "common/Export.h"
#include "common/Result.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eve {
class Value;
}
namespace eve::animation {
class AnimClip;
class AnimPose;
class AnimSkeleton;

/** @brief Stable authored control key; time is seconds, transform is parent-local. */
struct RigKey {
    double       time = 0;
    TransformTRS transform;
};
/** @brief Axis freedom relative to a constraint reference pose. */
enum class RigAxisMode { Free, Locked, Limited };
/** @brief Limited bounds include the neutral value (0 for position/angle, 1 for scale). */
struct RigAxisLimit {
    RigAxisMode mode    = RigAxisMode::Free;
    float       minimum = 0, maximum = 0;
};
/** @brief Owning parent-local limits; rotation is reference-relative XYZ in degrees.
 * @details Extension constrains position to reference.position times a positive ratio;
 * translation axes must be free in this mode. Reference is independent of animated initial.
 */
struct RigControlLimits {
    TransformTRS                reference;
    std::array<RigAxisLimit, 3> translation, rotation;
    RigAxisLimit                scale{RigAxisMode::Free, 1, 1};
    bool                        extension     = false;
    float                       minimumLength = 1, maximumLength = 1;
};
/** @brief Authored control or null. Empty parent means skeleton model space. */
struct RigControl {
    std::string         id, parent;
    TransformTRS        initial;
    std::vector<RigKey> keys;
    float               shapeSize = 0.12f;
    RigControlLimits    limits;
};
/** @brief Ordered rig operation; controls and bones use persistent names. */
enum class RigOperation { FK, Aim, TwoBoneIK };
/** @brief A solve node; after names prerequisite nodes. Stable declaration order breaks ties. */
struct RigNode {
    std::string              id, control, bone, middle, tip;
    RigOperation             operation = RigOperation::FK;
    float                    weight    = 1.f;
    std::vector<std::string> after;
};
/** @brief Owning, versioned rig authoring data; no runtime pointers or indices. */
struct RigDefinition {
    std::vector<RigControl> controls;
    std::vector<RigNode>    nodes;
    double                  duration = 2.0;
};

/**
 * @brief Compiled rig with an owned skeleton snapshot and indexed solve schedule.
 * @ownership Owns all retained inputs. Returned pose/transform values are independent.
 * @thread One owner thread per instance; independent instances may evaluate concurrently.
 * @reentrancy No callbacks. Inputs are borrowed only for each synchronous call.
 * @details Controls use positive uniform scale to avoid unrepresentable shear. Compilation
 * resolves names; evaluation uses indices. Time is supplied explicitly, clamped to duration.
 */
class EVENGINE_API_WORLD ControlRig {
public:
    /** @brief Destroy the owned compiled program and skeleton. */
    ~ControlRig();
    ControlRig(const ControlRig&)            = delete;
    ControlRig& operator=(const ControlRig&) = delete;
    /** @brief Validate and compile an owning rig snapshot; failure publishes nothing.
     * @cost Higher than evaluation; name resolution and graph sorting amortized per document revision.
     */
    [[nodiscard]] static Result<std::unique_ptr<ControlRig>> compile(const RigDefinition& definition,
                                                                     const AnimSkeleton&  skeleton);
    /** @brief Evaluate controls at seconds over input pose, publishing output only on success.
     * @param overrides Optional complete parent-local control array; empty samples authored tracks.
     * @return Structured diagnostic for incompatible pose, degenerate solve or invalid time/TRS.
     */
    [[nodiscard]] Result<void> evaluate(double seconds, const AnimPose& input, AnimPose& output,
                                        const std::vector<TransformTRS>& overrides = {}) const;
    /** @brief Sample parent-local control transforms in declaration order; returns owning values. */
    [[nodiscard]] Result<std::vector<TransformTRS>> sampleControls(double seconds) const;
    /** @brief Convert validated local controls to model space in declaration order. */
    [[nodiscard]] Result<std::vector<TransformTRS>> controlWorlds(const std::vector<TransformTRS>& locals) const;
    /** @brief Convert a model-space edit into the control's sampled parent space. */
    [[nodiscard]] Result<TransformTRS> localFromWorld(int index, const TransformTRS& world, double seconds) const;
    /** @brief Match unique full-weight FK controls to a pose; rejects non-FK or ambiguous graphs. */
    [[nodiscard]] Result<std::vector<TransformTRS>> matchFK(const AnimPose& pose) const;
    /** @brief Bake sampled rig output into a new caller-owned clip; no live clip is mutated.
     * @cost Offline work proportional to frames times solve nodes/bones; reuse the returned clip.
     */
    [[nodiscard]] Result<std::unique_ptr<AnimClip>> bake(double sampleRate, const AnimPose& input) const;

private:
    struct Impl;
    explicit ControlRig(std::unique_ptr<Impl> implementation);
    std::unique_ptr<Impl> impl_;
};

/** @brief Validate data-only rig constraints without a runtime skeleton. */
[[nodiscard]] EVENGINE_API_WORLD Result<void> validateRigDefinition(const RigDefinition& definition);
/** @brief Project a local TRS into limits, returning an owned value or invalid-config diagnostic.
 * @details Synchronous, thread-independent, no callbacks or retained references. Finite valid
 * transforms are clamped; invalid transforms/configuration fail. Cost is constant per control.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<TransformTRS> constrainRigControl(const RigControlLimits& limits,
                                                                          const TransformTRS&     transform);
/** @brief Encode a validated rig as eve.control-rig v2. Pure, no callbacks, owns result. */
[[nodiscard]] EVENGINE_API_WORLD Result<Value> encodeControlRig(const RigDefinition& definition);
/** @brief Decode atomically; rejects unknown versions/fields. Pure, no retained input references. */
[[nodiscard]] EVENGINE_API_WORLD Result<RigDefinition> decodeControlRig(const Value& value);
/** @brief Reparent a control while preserving its initial model transform; rejects animated controls.
 * @details Operates on an owning candidate, validates cycles, publishes only on success.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<void> reparentRigControl(RigDefinition& definition, std::string_view control,
                                                                 std::string_view parent);
}  // namespace eve::animation
