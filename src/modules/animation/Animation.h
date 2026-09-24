#pragma once
#include "common/Export.h"

#include "animation/MotionBuilder.h"
#include "animation/MotionSequence.h"
#include "animation/MotionRuntime.h"
#include "animation/MotionTypes.h"
#include "animation/Tween.h"
#include "common/Module.h"
#include "common/Time.h"

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

namespace eve::model3d {
class ModelData;
}

namespace eve::graphics {
class Graphics;
}

namespace eve::animation {

class AnimSkeleton;
class AnimClip;
class AnimPose;
class AnimPlayer;
class AnimGraph;
class AnimBoneMask;
class AnimLayerMixer;
class AnimStateMachine;
class MotionDatabase;
class MotionMatcher;
class ControlAnim;
class ControlPose;
class AnimSkin;
class AnimLattice;
class AnimTrail;
class AnimBatch;
class AnimConstraintStack;
class AnimSyncGroup;
class DynamicBoneSolver;
class FootIKSolver;
class SpriteSheet;
class SpriteClip;
class SpriteAnim;
class SpineAtlas;
class SpineSkeletonData;
class SpineSkeleton;
class SpineAnim;

/**
 * @brief Animation module — tween factory + 2D sprite-sheet / Spine + 3D skeletal
 * playback (player / state machine / motion matching) + control-theory
 * procedural drivers + motion trails + per-frame pump.
 * Script: `anim <- eve.Animation();`
 *
 * Tweens / SpriteAnim / SpineAnim can be advanced via the scheduler-owned
 * `anim.advance(SimulationStep)` API. The `update(float)` method remains a
 * compatibility facade and explicitly consumes the checked Result.
 * Relative property changes use `Tween::setDelta` / `setDeltaAngle`.
 *
 * 2D: `SpriteSheet` + `SpriteClip` + `SpriteAnim`; Spine region subset via
 * `SpineAtlas` / `SpineSkeletonData` / `SpineSkeleton` / `SpineAnim`.
 *
 * 3D: build `AnimSkeleton` + `AnimClip`, then drive with `AnimPlayer`,
 * `AnimStateMachine`, or `MotionMatcher` (+ `MotionDatabase`).
 * Procedural: `ControlAnim` (scalar second-order/PD/spring) and
 * `ControlPose` (pose tracking with the same control laws).
 * Trails: `AnimTrail` records samples and draws fading trajectories.
 */
class EVENGINE_API_WORLD Animation : public Module {
public:
    Module_REG(Animation);
    // Declared, not `= default`: class-level dllexport forces MSVC to compute the
    // defaulted constructor's exception specification, which needs the destructor of
    // every member -- and `SpriteSheet` is only forward-declared here (line 47), so an
    // in-class `= default` is C2027 "can't delete an incomplete type". Defined in
    // Animation.cpp, which includes animation/SpriteSheet.h.
    Animation();
    ~Animation() override;
    // `spriteSequenceCache_` is a container of `unique_ptr`: dllexport instantiates
    // every member, so the implicitly-defined copy operations would instantiate the
    // container's copy (hard C2280). Copy is deleted; no move is declared because
    // `MotionRuntime motions_` is itself neither copyable nor movable
    // (MotionRuntime.h:130 deletes copy and declares no move), which would make a
    // defaulted move operation ill-formed.
    Animation(const Animation &)            = delete;
    Animation &operator=(const Animation &) = delete;

    /** @brief Create a tween (duration in seconds). Returned pointer is owned by script GC. */
    Tween *newTween(float duration = 1.f);

    /** @brief 2D sprite-sheet animation factories (script GC owns returned objects). */
    SpriteSheet *newSpriteSheet();
    /**
     * @brief Load numbered PNG files into a runtime atlas and return its frame table.
     * Pattern must contain `{n}`, e.g. `fx/frame ({n}).png`. All frames must share
     * dimensions and RGBA8 format. `columns <= 0` chooses a near-square atlas.
     */
    SpriteSheet *newSpriteSheetFromSequence(eve::graphics::Graphics *gfx,
                                             const std::string &pattern, int first,
                                             int last, int columns = 0);
    /** @brief Import Aseprite/TexturePacker JSON frame metadata and its atlas texture. */
    SpriteSheet *newSpriteSheetFromAtlasJson(eve::graphics::Graphics *gfx,
                                              const std::string &texturePath,
                                              const std::string &jsonPath);
    SpriteClip  *newSpriteClip(const std::string &name = "");
    SpriteAnim  *newSpriteAnim();
    /** @brief Number of decoded runtime sequence atlases retained for reuse. */
    int getSpriteSequenceCacheCount() const;
    /** @brief Approximate RGBA8 bytes retained by cached sequence atlases. */
    int getSpriteSequenceCacheBytes() const;
    /** @brief Drop cache metadata; existing returned sheets/textures remain valid. */
    void clearSpriteSequenceCache();

    /** @brief Spine (region attachment subset) factories. */
    SpineAtlas        *newSpineAtlas();
    SpineSkeletonData *newSpineSkeletonData();
    SpineSkeleton     *newSpineSkeleton(SpineSkeletonData *data);
    SpineAnim         *newSpineAnim(SpineSkeleton *skeleton);

    /** @brief Load helpers (allocate + parse; false → empty object still returned? prefer nullable). */
    SpineAtlas        *newSpineAtlasFromFile(const std::string &path);
    SpineAtlas        *newSpineAtlasFromText(const std::string &text);
    SpineSkeletonData *newSpineSkeletonDataFromFile(const std::string &path);
    SpineSkeletonData *newSpineSkeletonDataFromJson(const std::string &json);

    /** @brief 3D skeletal animation factories (script GC owns returned objects). */
    AnimSkeleton     *newSkeleton();
    AnimClip         *newClip(const std::string &name = "");
    AnimPose         *newPose(int boneCount = 0);
    /** @brief Create a parallel batch clip evaluator. */
    AnimBatch        *newBatch();
    /** @brief Create an ordered procedural constraint stack. */
    AnimConstraintStack *newConstraintStack(AnimSkeleton* skeleton);
    /**
     * @brief Create a dynamic bone solver for spring-damped chains.
     * @ownership Returned object is owned by script GC.
     * @lifetime Valid until script GC releases it.
     */
    DynamicBoneSolver *newDynamicBoneSolver(AnimSkeleton *skeleton);

    /**
     * @brief Add a hair/tail spring chain with card-friendly defaults.
     * @return Chain index, or -1 on failure.
     */
    int setupHairChain(DynamicBoneSolver *solver, const std::string &rootBone,
                       const std::string &tipBone, float stiffness = 0.12f,
                       float damping = 0.18f, float inertia = 0.8f, float endLength = 0.08f,
                       bool selfCollision = true);

    /**
     * @brief Attach a head sphere collider for hair chains.
     * @return Collider count after add, or -1 on failure.
     */
    int setupHairHeadCollider(DynamicBoneSolver *solver, const std::string &boneName,
                              float radius = 0.12f);

    /**
     * @brief Create a paired-foot IK solver for a borrowed skeleton.
     * @ownership Returned object is owned by script GC.
     * @lifetime Valid until script GC releases it.
     */
    FootIKSolver *newFootIKSolver(AnimSkeleton *skeleton);
    /** @brief Create a normalized-time player synchronization group. */
    AnimSyncGroup* newSyncGroup();
    AnimPlayer       *newPlayer(AnimSkeleton *skeleton);
    /** @brief Create a composable pose graph for the skeleton. */
    AnimGraph        *newGraph(AnimSkeleton *skeleton);
    /** @brief Create a zero-weight per-bone animation mask. */
    AnimBoneMask* newBoneMask(AnimSkeleton* skeleton);
    /** @brief Create an override/additive animation layer mixer. */
    AnimLayerMixer*   newLayerMixer(AnimSkeleton* skeleton);
    AnimStateMachine *newStateMachine(AnimSkeleton *skeleton);
    MotionDatabase   *newMotionDatabase(AnimSkeleton *skeleton);
    MotionMatcher    *newMotionMatcher(AnimSkeleton *skeleton, MotionDatabase *database);

    /**
     * @brief Control-theory procedural animation (second-order / spring / PD).
     * frequencyHz: natural frequency f (Hz); dampingZeta: ζ; response: r.
     */
    ControlAnim *newControlAnim(float frequencyHz = 3.f, float dampingZeta = 1.f,
                                float response = 1.f);
    ControlPose *newControlPose(AnimSkeleton *skeleton);

    /**
     * @brief Import skeleton/clip from Assimp-backed ModelData, or from compact
     * `*.anim.txt` test fixtures (see AnimImporter).
     */
    AnimSkeleton *newSkeletonFromModel(eve::model3d::ModelData *model);
    AnimClip     *newClipFromModel(eve::model3d::ModelData *model, AnimSkeleton *skeleton,
                                   int animIndex = 0);
    /**
     * @brief Load a skeleton from an internal `*.anim.txt` test fixture.
     * @param path Filesystem path read synchronously during this call.
     * @return Newly allocated skeleton owned by the caller; never null on success.
     * @ownership Owned; the caller must delete the returned skeleton.
     * @lifetime Independent of this Animation module after construction.
     * @throws Exception when the fixture is missing, malformed, or unsupported.
     * @thread Main-thread animation API; no internal synchronization.
     * @reentrancy Does not invoke user callbacks.
     */
    AnimSkeleton *newSkeletonFromAnimationFixtureText(const std::string &path);
    /**
     * @brief Load an animation clip from an internal `*.anim.txt` test fixture.
     * @param path Filesystem path read synchronously during this call.
     * @return Newly allocated clip owned by the caller; never null on success.
     * @ownership Owned; the caller must delete the returned clip.
     * @lifetime Independent of this Animation module after construction; registered hot-reload
     *           tracking remains valid until the clip is destroyed.
     * @throws Exception when the fixture is missing, malformed, or unsupported.
     * @thread Main-thread animation API; no internal synchronization.
     * @reentrancy Does not invoke user callbacks while loading.
     */
    AnimClip *newClipFromAnimationFixtureText(const std::string &path);

    /**
     * @brief CPU linear-blend skin binding for a skinned mesh on ModelData.
     * meshIndex selects the Assimp mesh; bone names must match the skeleton.
     */
    AnimSkin *newSkinFromModel(eve::model3d::ModelData *model, int meshIndex,
                               AnimSkeleton *skeleton);

    /**
     * @brief 3D lattice scale-deformer (晶格缩放变形). divX/divY/divZ are the
     * control-point divisions (each >= 2). Control points are driven with
     * setPointScale / setPointOffset; bound vertices deform via trilinear
     * interpolation (see AnimLattice).
     */
    AnimLattice *newLattice(int divX = 2, int divY = 2, int divZ = 2);

    /** @brief newLattice bound to meshIndex of model (Assimp vertices). */
    AnimLattice *newLatticeFromModel(eve::model3d::ModelData *model, int meshIndex,
                                     int divX = 2, int divY = 2, int divZ = 2);

    /**
     * @brief Motion trail / afterimage (script GC owns returned object).
     * capacity: max retained samples (>= 2).
     */
    AnimTrail *newTrail(int capacity = 64);

    /**
     * @brief LitMotion-style float motion builder (C++ fluent API).
     * @example runtime().motion(0.f, 1.f, 0.5f).ease("outQuad").bind(sink);
     */
    [[nodiscard]] MotionBuilder motion(float from, float to, float duration);
    /** @brief LitMotion-style Vec2 motion builder. */
    [[nodiscard]] MotionVec2Builder motionVec2(MotionVec2 from, MotionVec2 to, float duration);
    /** @brief LitMotion-style Vec3 motion builder. */
    [[nodiscard]] MotionVec3Builder motionVec3(MotionVec3 from, MotionVec3 to, float duration);

    /** @brief Create an empty LitMotion-style motion sequence on this module runtime. */

    /**
     * @brief LitMotion-style punch (damped sine about `from`, strength=`strength`).
     * @example runtime().punch(0.f, 12.f, 0.4f).frequency(18).dampingRatio(0.f).bind(sink);
     */
    [[nodiscard]] MotionBuilder punch(float from, float strength, float duration);
    /** @brief LitMotion-style shake (punch with deterministic random signs). */
    [[nodiscard]] MotionBuilder shake(float from, float strength, float duration);
    /** @brief Vec2 punch builder. */
    [[nodiscard]] MotionVec2Builder punchVec2(MotionVec2 from, MotionVec2 strength, float duration);
    /** @brief Vec2 shake builder. */
    [[nodiscard]] MotionVec2Builder shakeVec2(MotionVec2 from, MotionVec2 strength, float duration);
    /** @brief Vec3 punch builder. */
    [[nodiscard]] MotionVec3Builder punchVec3(MotionVec3 from, MotionVec3 strength, float duration);
    /** @brief Vec3 shake builder. */
    [[nodiscard]] MotionVec3Builder shakeVec3(MotionVec3 from, MotionVec3 strength, float duration);
    /** @brief Color tween builder (lerp). */
    [[nodiscard]] MotionColorBuilder motionColor(MotionColor from, MotionColor to, float duration);
    /** @brief Quaternion tween builder (slerp). */
    [[nodiscard]] MotionQuatBuilder motionQuat(MotionQuat from, MotionQuat to, float duration);

    [[nodiscard]] MotionSequence sequence();

    /** @brief Shared motion storage for builders and handle queries. */
    [[nodiscard]] MotionRuntime &motions() noexcept { return motions_; }
    [[nodiscard]] const MotionRuntime &motions() const noexcept { return motions_; }

    /** @brief Advance all registered tweens, motions, sprite anims, and spine anims. */
    [[nodiscard]] eve::Result<void> advance(const eve::SimulationStep &step);

    /** @brief Last scheduler tick consumed by the checked module pump. */
    [[nodiscard]] eve::SimulationTick currentTick() const noexcept { return lastTick_; }

    /** @brief Legacy seconds facade; invalid input is explicitly consumed and ignored. */
    void update(float dt);

    int getTweenCount() const { return static_cast<int>(tweens_.size()); }
    int getMotionCount() const { return motions_.activeCount(); }

    /**
     * @brief Pre-size motion float pool for churn-free spawn (Phase 4).
     * @param floatCount Target number of float slots (Inactive + free-list).
     */
    void ensureMotionCapacity(std::size_t floatCount) { motions_.ensureCapacity(floatCount); }
    [[nodiscard]] std::size_t getMotionFloatCapacity() const noexcept {
        return motions_.floatCapacity();
    }
    [[nodiscard]] std::size_t getMotionFloatFreeCount() const noexcept {
        return motions_.floatFreeCount();
    }
    int getSpriteAnimCount() const { return static_cast<int>(spriteAnims_.size()); }
    int getSpineAnimCount() const { return static_cast<int>(spineAnims_.size()); }
    int getActiveCount() const;

    /** @brief Drop finished/stopped entries from the registry (does not delete Tween objects). */
    void clearFinished();
    /** @brief Detach all tweens from the registry (does not delete Tween objects). */
    void clearAll();

private:
    std::unordered_map<std::string, std::unique_ptr<SpriteSheet>> spriteSequenceCache_;
    friend class Tween;
    friend class SpriteAnim;
    friend class SpineAnim;

    void registerTween(Tween *t);
    void unregisterTween(Tween *t);
    void registerSpriteAnim(SpriteAnim *a);
    void unregisterSpriteAnim(SpriteAnim *a);
    void registerSpineAnim(SpineAnim *a);
    void unregisterSpineAnim(SpineAnim *a);

    std::vector<Tween *>      tweens_;
    std::vector<SpriteAnim *> spriteAnims_;
    std::vector<SpineAnim *>  spineAnims_;
    MotionRuntime             motions_;
    eve::SimulationTick       lastTick_    = eve::SimulationTick::zero();
    bool                      hasLastTick_ = false;
};

}  // namespace eve::animation
