#pragma once

#include "common/Module.h"
#include "animation/Tween.h"

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
class AnimStateMachine;
class MotionDatabase;
class MotionMatcher;
class ControlAnim;
class ControlPose;
class AnimSkin;
class AnimLattice;
class AnimTrail;
class AnimBatch;
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
 * Tweens / SpriteAnim / SpineAnim can be advanced via `anim.update(dt)`.
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
class Animation : public Module {
public:
    Module_REG(Animation);
    Animation() = default;
    ~Animation() override;

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
    AnimPlayer       *newPlayer(AnimSkeleton *skeleton);
    /** @brief Create a composable pose graph for the skeleton. */
    AnimGraph        *newGraph(AnimSkeleton *skeleton);
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
     * @brief Import skeleton/clip from Assimp-backed ModelData, or from compact `.eva`
     * fixtures (see AnimImporter).
     */
    AnimSkeleton *newSkeletonFromModel(eve::model3d::ModelData *model);
    AnimClip     *newClipFromModel(eve::model3d::ModelData *model, AnimSkeleton *skeleton,
                                   int animIndex = 0);
    AnimSkeleton *newSkeletonFromEvaFile(const std::string &path);
    AnimClip     *newClipFromEvaFile(const std::string &path);

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

    /** @brief Advance all registered tweens, sprite anims, and spine anims. */
    void update(float dt);

    int getTweenCount() const { return static_cast<int>(tweens_.size()); }
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
};

}  // namespace eve::animation
