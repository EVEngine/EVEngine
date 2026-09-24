#pragma once
#include "common/Export.h"


#include "animation/AnimPose.h"
#include "animation/AnimPoseSource.h"
#include "common/Time.h"

#include <string>
#include <vector>

namespace eve::animation {

class AnimGraph;
class AnimPlayer;
class AnimSkeleton;
class AnimStateMachine;

/** @brief Per-bone weights used by override and additive animation layers. */
class EVENGINE_API_WORLD AnimBoneMask {
public:
    /** @brief Create a zero-weight mask for a skeleton; the skeleton is not owned. */
    explicit AnimBoneMask(AnimSkeleton* skeleton);

    /** @brief Set every bone to the same clamped weight. */
    void setAll(float weight);
    /** @brief Set one bone's clamped weight. */
    bool setBoneWeight(int boneIndex, float weight);
    /** @brief Set a named bone's clamped weight. */
    bool setBoneWeightByName(const std::string& boneName, float weight);
    /** @brief Set a bone and all descendants to the same clamped weight. */
    bool setBoneAndChildren(const std::string& boneName, float weight);
    /** @brief Return one bone's weight, or zero for an invalid index. */
    float getBoneWeight(int boneIndex) const;
    /** @brief Return the number of bones represented by this mask. */
    int getBoneCount() const { return static_cast<int>(weights_.size()); }
    /** @brief Return the non-owned skeleton used by this mask. */
    AnimSkeleton* getSkeleton() const { return skeleton_; }

private:
    AnimSkeleton*      skeleton_ = nullptr;
    std::vector<float> weights_;
};

/**
 * @brief Combines a borrowed base pose source with ordered override/additive layers.
 *
 * Pose sources such as AnimPlayer, AnimGraph, AnimStateMachine, or a nested mixer,
 * plus masks/clips/skeleton, are referenced but not owned. The mixer owns the
 * evaluation boundary: call advance once per tick and do not advance attached
 * sources separately.
 */
class EVENGINE_API_WORLD AnimLayerMixer : public IAnimPoseSource {
public:
    /** @brief Create an empty mixer for a skeleton; the skeleton is not owned. */
    explicit AnimLayerMixer(AnimSkeleton* skeleton);

    /**
     * @brief Set the borrowed base pose source evaluated before layers.
     * @return Applied on success; Conflict/InvalidArgument on failure.
     */
    [[nodiscard]] eve::Result<void> setBaseSource(IAnimPoseSource* source);
    /** @brief Compatibility facade over setBaseSource(AnimPlayer*). */
    bool setBasePlayer(AnimPlayer* player);
    /** @brief Compatibility facade over setBaseSource(AnimGraph*). */
    bool setBaseGraph(AnimGraph* graph);
    /** @brief Compatibility facade over setBaseSource(AnimStateMachine*). */
    bool setBaseStateMachine(AnimStateMachine* stateMachine);
    /**
     * @brief Return the borrowed base source, or null.
     * @ownership Borrowed
     * @lifetime Valid while the owning animation object remains alive; do not retain across destruction.
     */
    IAnimPoseSource* getBaseSource() const { return baseSource_; }
    /**
     * @brief Return the base when it is an AnimPlayer; otherwise null.
     * @ownership Borrowed
     * @lifetime Valid while the owning animation object remains alive; do not retain across destruction.
     */
    AnimPlayer* getBasePlayer() const;

    /**
     * @brief Append a pose layer. mode is "override" or "additive".
     * A null mask affects every bone. Additive layers default to BindPose reference.
     */
    [[nodiscard]] eve::Result<int> addPoseLayer(const std::string& name, IAnimPoseSource* source, AnimBoneMask* mask,
                                                const std::string& mode);
    /** @brief Compatibility facade over addPoseLayer(AnimPlayer*). */
    int addLayer(const std::string& name, AnimPlayer* player, AnimBoneMask* mask, const std::string& mode);
    /** @brief Compatibility facade over addPoseLayer(AnimGraph*). */
    int addGraphLayer(const std::string& name, AnimGraph* graph, AnimBoneMask* mask, const std::string& mode);
    /** @brief Compatibility facade over addPoseLayer(AnimStateMachine*). */
    int addStateMachineLayer(const std::string& name, AnimStateMachine* stateMachine, AnimBoneMask* mask,
                             const std::string& mode);

    /** @brief Remove a layer by name. */
    bool removeLayer(const std::string& name);
    /** @brief Set a layer's global clamped weight. */
    bool setLayerWeight(const std::string& name, float weight);
    /** @brief Enable or disable a layer without removing it. Disabled layers still advance. */
    bool setLayerEnabled(const std::string& name, bool enabled);
    /**
     * @brief Set additive reference for an additive layer: "bind" or "identity".
     * @return Applied on success; NotFound/InvalidArgument otherwise.
     */
    [[nodiscard]] eve::Result<void> setLayerAdditiveReference(const std::string& name, const std::string& reference);
    /** @brief Compatibility facade over setLayerAdditiveReference. */
    bool setLayerAdditiveReferenceCompat(const std::string& name, const std::string& reference);

    /** @brief Return the number of configured layers. */
    int getLayerCount() const { return static_cast<int>(layers_.size()); }
    /** @brief Return a layer name, or empty for an invalid index. */
    std::string getLayerName(int index) const;
    /** @brief Return a layer weight, or zero for an unknown name. */
    float getLayerWeight(const std::string& name) const;
    /** @brief Return whether a layer is enabled; false for unknown names. */
    bool getLayerEnabled(const std::string& name) const;
    /** @brief Return "override" / "additive", or empty for unknown names. */
    std::string getLayerMode(const std::string& name) const;
    /** @brief Return "bind" / "identity", or empty for unknown names. */
    std::string getLayerAdditiveReference(const std::string& name) const;

    /** @brief Advance all referenced sources (including disabled layers) and compose. */
    [[nodiscard]] eve::Result<void> advance(const eve::SimulationStep& step) override;
    /** @brief Legacy seconds facade; explicitly forwards to advance(). */
    void update(float dt);
    /**
     * @brief Return the most recently combined pose.
     * @ownership Borrowed
     * @lifetime Valid while the owning animation object remains alive; do not retain across destruction.
     */
    AnimPose* getPose() override { return &pose_; }
    /**
     * @brief Return the mixer skeleton.
     * @ownership Borrowed
     * @lifetime Valid while the owning animation object remains alive; do not retain across destruction.
     */
    AnimSkeleton*                     getSkeleton() const override { return skeleton_; }
    [[nodiscard]] bool                hasCurrentTick() const noexcept override { return hasLastTick_; }
    [[nodiscard]] eve::SimulationTick currentTick() const noexcept override { return lastTick_; }
    [[nodiscard]] int                 getEventCount() const override { return static_cast<int>(events_.size()); }
    [[nodiscard]] std::string         getEventName(int index) const override;
    [[nodiscard]] std::string         getEventPayload(int index) const override;

    /** @brief Source layer name; "base" identifies the base source. */
    std::string getEventLayer(int index) const;
    /** @brief Clear events collected by the most recent update. */
    void clearEvents() { events_.clear(); }

private:
    struct Layer {
        std::string           name;
        IAnimPoseSource*      source            = nullptr;
        AnimBoneMask*         mask              = nullptr;
        float                 weight            = 1.f;
        bool                  additive          = false;
        bool                  enabled           = true;
        AnimAdditiveReference additiveReference = AnimAdditiveReference::BindPose;
    };
    struct Event {
        std::string layer;
        std::string name;
        std::string payload;
    };

    /** @ownership Borrowed @lifetime Valid until layers_ mutates. */
    Layer* findLayer(const std::string& name);
    /** @ownership Borrowed @lifetime Valid until layers_ mutates. */
    const Layer*                    findLayer(const std::string& name) const;
    void                            collectEvents(const std::string& layerName, IAnimPoseSource* source);
    void                            applyOverride(const Layer& layer);
    void                            applyAdditive(const Layer& layer);
    [[nodiscard]] eve::Result<void> attachSource(IAnimPoseSource* source, const char* role);
    bool                            sourceAlreadyAttached(const IAnimPoseSource* source) const;

    AnimSkeleton*      skeleton_   = nullptr;
    IAnimPoseSource*    baseSource_ = nullptr;
    AnimPose           pose_;
    std::vector<Layer> layers_;
    std::vector<Event> events_;
    eve::SimulationTick lastTick_    = eve::SimulationTick::zero();
    bool                hasLastTick_ = false;

    void compose();
};

}  // namespace eve::animation
