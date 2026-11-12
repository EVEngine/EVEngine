// Native pose solver adapted from pixelfx core/rig.js, MIT. Copyright (c) 2026 Patch.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>
#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"

namespace eve::stylize::pixelfx::rig {
/** @brief Double precision local 3D vector; solver y points up. */
using Vec = std::array<double, 3>;
/** @brief Row-major 3x3 rotation matrix. */
using Mat = std::array<double, 9>;
/** @brief Authored weapon families with source-owned grip geometry. */
enum class Weapon { None, Sword, Greatsword, Dagger, Spear, Hammer, Scythe, Claw, Bow, Pistol, Launcher, Handle };
/** @brief Screen-space pose inputs copied by solve; no entity, script or callback is retained. */
struct Pose {
    double lean = 0, crouch = 0, yaw = 0, fx = -3, fy = -15, bx = 5, by = -15, ffx = -2, ffy = -4, fbx = 3, fby = -4;
    double x = 0, y = 0, sa = 0, wl = 1, wz = 1, twist = 0, hy = 0, hx = 0, sb = 3.14159265358979323846, ghost = 0;
    std::optional<double> turn, fz, bz, fzz, bzz;
    Weapon                weapon = Weapon::None, offWeapon = Weapon::None;
    bool                  two = false, armsFront = false, toeF = false, toeB = false, pull = false, nocked = false;
    int                   face = 1;
    /** @brief Compare all owning scalar/optional pose values; no padding, allocation or callbacks. */
    bool operator==(const Pose&) const = default;
};
/** @brief Typed source pose properties; ordinal bits are process-local and are not a persistent format. */
enum class PoseField {
    Lean,
    Crouch,
    Yaw,
    Fx,
    Fy,
    Bx,
    By,
    Ffx,
    Ffy,
    Fbx,
    Fby,
    X,
    Y,
    Sa,
    Wl,
    Wz,
    Twist,
    Hy,
    Hx,
    Sb,
    Turn,
    Fz,
    Bz,
    Fzz,
    Bzz,
    Weapon,
    OffWeapon,
    Two,
    ArmsFront,
    ToeF,
    ToeB,
    Pull,
    Nocked,
    Ghost,
    Face,
    Count
};
/** @brief Owning effective rig pose plus exact source poseAt defined-property presence, independent of track lifetime.
 * @note Omitted properties retain rig defaults in pose; contains distinguishes omission from authored/defaulted values.
 * Interpolated samples include STAND and numeric later-key properties, while endpoints/snap retain raw authored keys.
 */
struct AuthoredPose {
    Pose     pose;
    uint64_t presence = 0;
    /** @brief Query a typed source property with a defined value; an invalid identity is absent. No allocation or
     * retained borrows. */
    [[nodiscard]] bool contains(PoseField field) const noexcept {
        const auto index = size_t(field);
        return index < size_t(PoseField::Count) && (presence & (uint64_t(1) << index));
    }
    /** @brief Compare effective values and source-defined property presence. */
    bool operator==(const AuthoredPose&) const = default;
};
/** @brief Borrow immutable codec names in PoseField order, valid for process lifetime; concurrent reads are safe.
 * @return Non-owning metadata span; no mutable state, callbacks or allocation after constant-sized initialization. */
[[nodiscard]] EVENGINE_API_WORLD std::span<const std::string_view> poseFieldNames();
/** @brief Return an owning source STAND pose and its defined fields; no allocation, time, RNG or callbacks. */
[[nodiscard]] EVENGINE_API_WORLD AuthoredPose standPose();
/** @brief Immutable owning compiled pose track; sample time is supplied explicitly. */
class PoseTrack {
public:
    /** @brief Release compiled keys; previously sampled poses are independent. */
    EVENGINE_API_WORLD ~PoseTrack();
    /** @brief Transfer compiled ownership; source is valid only for destruction/assignment. */
    EVENGINE_API_WORLD PoseTrack(PoseTrack&&) noexcept;
    /** @brief Release current keys and transfer ownership. */
    EVENGINE_API_WORLD PoseTrack& operator=(PoseTrack&&) noexcept;
    PoseTrack(const PoseTrack&)            = delete;
    PoseTrack& operator=(const PoseTrack&) = delete;
    /** @brief Prepare an owning eve.pixelfx.pose-track v1 schema/version/keys document.
     * @param document Borrowed only during synchronous validation; no Value nodes survive.
     * @return Immutable compiled track or field diagnostic; no partial publication.
     * @note Equal-time keys are preserved in source order; decreasing times reject. Allocation failure may throw
     * std::bad_alloc.
     * @cost Copies and compiles up to 4096 authored keys once, amortized over sampling. */
    [[nodiscard]] EVENGINE_API_WORLD static Result<PoseTrack> prepare(const Value& document);
    /** @brief Sample source 12-FPS held limbs with smoothly interpolated floor position.
     * @param seconds Explicit finite track time within -1e12..1e12; no clock or RNG is consulted.
     * @return Owning effective rig pose; weapon/boolean metadata holds until the later key.
     * @note Const reads have no mutable cache, retained references, callbacks or allocation.
     * @cost Linear key search and a fixed scalar-field pass per sample; no raster work. */
    [[nodiscard]] EVENGINE_API_WORLD Result<Pose> sample(double seconds) const;
    /** @brief Sample the same interpolation with source defined-property presence for Act shadow/carrying composition.
     * @param seconds Explicit finite time in -1e12..1e12; invalid time returns a field diagnostic.
     * @return Independent owning effective pose and source property mask, valid after track destruction/move.
     * @note Const reads are thread-safe; no retained pointers, callbacks, mutable cache or allocation.
     * @cost Same linear key search and fixed field pass as sample; no raster work. */
    [[nodiscard]] EVENGINE_API_WORLD Result<AuthoredPose> sampleAuthored(double seconds) const;
    /** @brief Query the source held-pose condition at explicit time; no mutable state or callbacks.
     * @cost Linear key search with fixed scalar comparisons; no raster allocation. */
    [[nodiscard]] EVENGINE_API_WORLD Result<bool> isHeld(double seconds) const;

private:
    struct State;
    std::unique_ptr<const State> state_;
    explicit PoseTrack(std::unique_ptr<const State> state);
};
/** @brief Build the source posed-cache identity: defined fields, rounded numeric values and airborne clipping.
 * @note Pure process-local identity, not a persisted format; ignores x/y/ghost except y's clipping threshold.
 * @cost One fixed scalar-field pass with no allocations or raster work. */
[[nodiscard]] EVENGINE_API_WORLD AuthoredPose rasterCacheKey(const AuthoredPose& pose);
/** @brief Stable source frame order used by rest-pixel mapping. */
enum class Bone {
    Pelvis,
    Waist,
    Chest,
    Head,
    Neck,
    UpperN,
    ForeN,
    UpperF,
    ForeF,
    ThighN,
    ShinN,
    ThighF,
    ShinF,
    FootN,
    FootF,
    Count
};
/** @brief A bone-local coordinate frame; no shared mutable storage. */
struct Frame {
    Vec t{};
    Mat R{};
};
/** @brief Two-segment inverse-kinematics result, with pole basis and clamped end point. */
struct Limb {
    Vec joint{}, end{}, u{}, v{};
};
/** @brief Owning solved pose; all joints and frames are expressed in y-up local space. */
struct Solved {
    Pose                                   pose;
    std::array<Frame, size_t(Bone::Count)> frames{};
    Vec                                    axis{}, side{}, shoulderN{}, shoulderF{}, hipN{}, hipF{};
    Limb                                   armN{}, armF{}, legN{}, legF{};
    double                                 turn = 0;
};
/** @brief Source joint order; coordinates returned by skeleton use screen y-down. */
enum class Joint {
    Pelvis,
    Waist,
    Chest,
    Neck,
    Head,
    Crown,
    ShoulderN,
    ElbowN,
    HandN,
    ShoulderF,
    ElbowF,
    HandF,
    HipN,
    KneeN,
    AnkleN,
    ToeN,
    HipF,
    KneeF,
    AnkleF,
    ToeF,
    Count
};
/** @brief Owning screen-space joint positions; no raw references to the solver. */
struct Skeleton {
    std::array<Vec, size_t(Joint::Count)> joints{};
};
/** @brief Borrow immutable joint names for process lifetime; order matches Joint. */
[[nodiscard]] EVENGINE_API_WORLD std::span<const std::string_view> jointNames();
/** @brief Solve and rotate an owning screen-space skeleton; input is borrowed only until return.
 * @cost Fixed scalar work per pose; no raster allocation or callbacks. */
[[nodiscard]] EVENGINE_API_WORLD Result<Skeleton> skeleton(const Pose& pose);
/** @brief Owning sparse posed pixels with standing-body source indices; all arrays have color.size() entries. */
struct Pixels {
    std::vector<int16_t>  xs, ys, src;
    std::vector<uint32_t> color;
    std::vector<uint8_t>  ink;
    int                   x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    double                z = 0;
};
/** @brief Independent posed part with its own outline; name borrows a process-lifetime literal. */
struct PartPixels {
    std::string_view name;
    int              part = 0;
    Pixels           pixels;
    double           centerX = 0, centerY = 0;
};
/** @brief Caller-thread-affine owner of bounded raster scratch; no callbacks or process-global mutable cache. */
class Rasterizer {
public:
    /** @brief Allocate the source 300x240 depth grid and derive immutable rest frames.
     * @cost Allocates several bounded scratch planes once per rasterizer, amortized across poses. */
    EVENGINE_API_WORLD Rasterizer();
    /** @brief Release scratch; previously returned pixel snapshots retain independent ownership. */
    EVENGINE_API_WORLD ~Rasterizer();
    /** @brief Transfer scratch ownership; source is valid only for destruction or assignment. */
    EVENGINE_API_WORLD Rasterizer(Rasterizer&&) noexcept;
    /** @brief Release scratch and transfer ownership from source. */
    EVENGINE_API_WORLD Rasterizer& operator=(Rasterizer&&) noexcept;
    Rasterizer(const Rasterizer&)            = delete;
    Rasterizer& operator=(const Rasterizer&) = delete;
    /** @brief Solve and rasterize a pose into an owning sparse snapshot.
     * @param pose Borrowed only during this synchronous call.
     * @return Complete pixels or diagnostic; invalid/scratch-exceeding geometry publishes no partial snapshot.
     * @note Previous snapshots are independent; caller-thread affine, no callbacks or retained input references.
     * @cost Rasterizes all body and weapon primitives and allocates sparse output once per explicit pose evaluation. */
    [[nodiscard]] EVENGINE_API_WORLD Result<Pixels> render(const Pose& pose);
    /** @brief Render each present body/weapon part alone, including its independent outline.
     * @param pose Borrowed for the synchronous evaluation; no references are retained.
     * @return Owning part snapshots in source order; failure publishes no partial part list.
     * @note Caller-thread affine; does not affect earlier snapshots or subsequent full renders.
     * @cost Up to 24 complete filtered raster passes and sparse allocations per explicit exploded-view build. */
    [[nodiscard]] EVENGINE_API_WORLD Result<std::vector<PartPixels>> renderParts(const Pose& pose);

private:
    Result<Pixels> renderSelected(const Pose& pose, int part);
    struct State;
    std::unique_ptr<State> state_;
};
/**
 * @brief Validate a pose and solve its fixed-length limbs and body frames without publication.
 * @param pose Borrowed for this synchronous call; every input is copied before return.
 * @return Owning solved state or field diagnostic; no externally observable state changes on failure.
 * @note Caller-thread affine by convention; this pure solver has no globals, RNG, time or callbacks.
 * @cost Fixed work per pose, including up to sixteen pole candidates per armed hand; no raster allocation.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<Solved> solve(const Pose& pose);
/** @brief Encode an owning eve.pixelfx.pose v1 input document; no pose references are retained.
 * @cost Copies a fixed set of scalar pose fields once per explicit serialization. */
[[nodiscard]] EVENGINE_API_WORLD Value encodePose(const Pose& pose);
/** @brief Decode and validate an owning replacement pose; missing fields use STAND defaults.
 * @param document Borrowed schema/version/fields object; unknown fields and versions reject.
 * @return Owning pose or structured diagnostic, with no partially published state.
 * @cost Validates a fixed set of scalar fields once per explicit decode; no raster allocation. */
[[nodiscard]] EVENGINE_API_WORLD Result<Pose> decodePose(const Value& document);
/** @brief Rotate an owned solved pose about its vertical foot axis; no callbacks or allocations.
 * @param state Owned mutable state; no references are retained.
 * @param angle Finite radians, validated by the caller's pose boundary. */
EVENGINE_API_WORLD void turnAll(Solved& state, double angle);
/** @brief Return the greatest hand-target shortfall for a solved pose, in logical pixels. */
[[nodiscard]] EVENGINE_API_WORLD double shortfall(const Solved& state);
}  // namespace eve::stylize::pixelfx::rig
