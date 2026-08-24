#pragma once

#include "animation/AnimPose.h"

#include <string>
#include <vector>

namespace eve::animation {

class AnimPlayer;
class AnimSkeleton;

/** @brief Per-bone weights used by override and additive animation layers. */
class AnimBoneMask {
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
 * @brief Combines a base AnimPlayer with ordered override/additive layers.
 * Players, masks, clips, and skeleton are referenced but not owned.
 */
class AnimLayerMixer {
public:
    /** @brief Create an empty mixer for a skeleton; the skeleton is not owned. */
    explicit AnimLayerMixer(AnimSkeleton* skeleton);

    /** @brief Set the base player whose pose is evaluated before layers. */
    bool setBasePlayer(AnimPlayer* player);
    /** @brief Return the non-owned base player. */
    AnimPlayer* getBasePlayer() const { return basePlayer_; }
    /**
     * @brief Append a layer. mode is "override" or "additive".
     * A null mask affects every bone.
     * @return layer index, or -1 when arguments are invalid.
     */
    int addLayer(const std::string& name, AnimPlayer* player, AnimBoneMask* mask, const std::string& mode);
    /** @brief Remove a layer by name. */
    bool removeLayer(const std::string& name);
    /** @brief Set a layer's global clamped weight. */
    bool setLayerWeight(const std::string& name, float weight);
    /** @brief Enable or disable a layer without removing it. */
    bool setLayerEnabled(const std::string& name, bool enabled);
    /** @brief Return the number of configured layers. */
    int getLayerCount() const { return static_cast<int>(layers_.size()); }
    /** @brief Return a layer name, or empty for an invalid index. */
    std::string getLayerName(int index) const;
    /** @brief Advance referenced players and build the combined pose. */
    void update(float dt);
    /** @brief Return the most recently combined pose. */
    AnimPose* getPose() { return &pose_; }
    /** @brief Return the mixer skeleton. */
    AnimSkeleton* getSkeleton() const { return skeleton_; }

    /** @brief Number of events emitted by base/layer players in the latest update. */
    int getEventCount() const { return static_cast<int>(events_.size()); }
    /** @brief Source layer name; "base" identifies the base player. */
    std::string getEventLayer(int index) const;
    /** @brief Event marker name, or empty for an invalid index. */
    std::string getEventName(int index) const;
    /** @brief Event payload, or empty for an invalid index. */
    std::string getEventPayload(int index) const;
    /** @brief Clear events collected by the most recent update. */
    void clearEvents() { events_.clear(); }

private:
    struct Layer {
        std::string   name;
        AnimPlayer*   player   = nullptr;
        AnimBoneMask* mask     = nullptr;
        float         weight   = 1.f;
        bool          additive = false;
        bool          enabled  = true;
    };
    struct Event {
        std::string layer;
        std::string name;
        std::string payload;
    };

    Layer* findLayer(const std::string& name);
    void   collectEvents(const std::string& layerName, AnimPlayer* player);
    void   applyOverride(const Layer& layer);
    void   applyAdditive(const Layer& layer);

    AnimSkeleton*      skeleton_   = nullptr;
    AnimPlayer*        basePlayer_ = nullptr;
    AnimPose           pose_;
    std::vector<Layer> layers_;
    std::vector<Event> events_;
};

}  // namespace eve::animation
