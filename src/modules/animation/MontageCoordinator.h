#pragma once

/** @file MontageCoordinator.h @brief Layered dual-slot montage ownership and stable handles. */

#include "animation/MontagePlayer.h"
#include "common/RuntimeHandle.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace eve::animation {

class AnimBoneMask;

struct MontageHandleTag {};
/** @brief Generation-qualified identity owned by one MontageCoordinator. */
using MontageHandle = RuntimeHandle<MontageHandleTag>;

/**
 * @brief Owns a fixed two-slot ping-pong topology for every animation layer.
 *
 * The coordinator is owner-thread-only and borrows its skeleton for its whole
 * lifetime. Reusing a slot bumps its generation, so old handles resolve as
 * stale instead of targeting a different montage.
 */
class MontageCoordinator {
public:
    /** @brief Construct over a skeleton that must outlive this coordinator. */
    explicit MontageCoordinator(AnimSkeleton& skeleton);

    /**
     * @brief Prepare and play a montage in the opposite slot of a layer.
     * @param tick Monotonic simulation tick used when the previous slot begins fading out.
     */
    [[nodiscard]] Result<MontageHandle> play(std::size_t layer, action::ActionTimeline timeline,
                                             std::vector<MontageClipAsset>&& clips,
                                             action::ActionExecutionId executionId, SimulationTick tick);
    /** @brief Resolve a live handle for immediate synchronous use. */
    [[nodiscard]] Result<std::reference_wrapper<MontagePlayer>> resolve(MontageHandle handle);
    /** @brief Resolve a live handle for immediate synchronous read-only use. */
    [[nodiscard]] Result<std::reference_wrapper<const MontagePlayer>> resolve(MontageHandle handle) const;
    /** @brief Present one authoritative action advance through a live slot. */
    [[nodiscard]] Result<MontageAdvance> present(MontageHandle handle, const action::ActionAdvance& advance,
                                                 SimulationTick tick);
    /** @brief Advance every fading slot except an optional slot already advanced by its owner. */
    [[nodiscard]] Result<void> advanceBlendOuts(Duration delta, SimulationTick tick,
                                                MontageHandle except = MontageHandle::invalid());
    /** @brief Stop one live slot with a continuous blend-out. */
    [[nodiscard]] Result<MontageAdvance> stop(MontageHandle handle, Duration blendOut, SimulationTick tick);
    /**
     * @brief Atomically replace one layer's owning per-bone blend weights.
     * @param layer Layer index configured independently from montage slot generations.
     * @param weights One finite weight in [0,1] for every skeleton bone.
     */
    [[nodiscard]] Result<void> setLayerBoneMask(std::size_t layer, std::vector<float> weights);
    /**
     * @brief Snapshot an existing animation bone mask into one montage layer.
     * @param layer Layer index.
     * @param mask Borrowed only for this synchronous call; it must target the coordinator skeleton.
     */
    [[nodiscard]] Result<void> setLayerBoneMask(std::size_t layer, const AnimBoneMask& mask);
    /** @brief Remove one layer's bone mask so subsequent composition affects every bone. */
    [[nodiscard]] Result<void> clearLayerBoneMask(std::size_t layer);
    /** @brief Return an owning copy of one configured layer mask. */
    [[nodiscard]] Result<std::vector<float>> layerBoneMask(std::size_t layer) const;
    /**
     * @brief Set one layer's finite global influence.
     * @param layer Layer index, created on demand.
     * @param weight Influence in [0,1].
     * @return Applied, NoOp, or InvalidArgument without partially changing configuration.
     */
    [[nodiscard]] Result<void> setLayerWeight(std::size_t layer, float weight);
    /** @brief Return one configured layer's global influence, or NotFound for an absent layer. */
    [[nodiscard]] Result<float> layerWeight(std::size_t layer) const;
    /**
     * @brief Select bind-pose-relative additive or ordered override composition for one layer.
     * @param layer Layer index, created on demand.
     * @param additive True for additive; false for override.
     * @return Applied or NoOp, or InvalidArgument for an unrepresentable layer index.
     */
    [[nodiscard]] Result<void> setLayerAdditive(std::size_t layer, bool additive);
    /** @brief Return whether one configured layer uses additive composition, or NotFound for an absent layer. */
    [[nodiscard]] Result<bool> layerAdditive(std::size_t layer) const;
    /**
     * @brief Install one root-motion receiver shared by current and future slots of a layer.
     * @param layer Layer index.
     * @param receiver Receiver that must outlive the coordinator or be explicitly cleared.
     * @lifetime The coordinator and its players retain a non-owning observer until clearLayerRootMotionReceiver().
     */
    [[nodiscard]] Result<void> setLayerRootMotionReceiver(std::size_t layer,
                                                          IMontageRootMotionReceiver& receiver);
    /** @brief Clear the borrowed root-motion receiver from current and future slots of a layer. */
    [[nodiscard]] Result<void> clearLayerRootMotionReceiver(std::size_t layer);
    /** @brief Borrow the normalized full layer pose; invalid layers return a structured failure. */
    [[nodiscard]] Result<std::reference_wrapper<AnimPose>> pose(std::size_t layer);
    /**
     * @brief Compose every configured montage layer over an immediate borrowed base pose.
     * @param basePose Base pose for this synchronous evaluation; it is never retained and must match the skeleton.
     * @return Borrowed coordinator-owned pose, valid until the next composition or coordinator destruction.
     * @details Layers are applied in ascending index order. Slot weights are normalized within each layer; their
     * clamped sum, the layer weight, and the optional bone mask determine top-level influence. Additive layers use
     * bind-pose-relative deltas. Owner-thread-only, deterministic for identical poses and weights, and non-reentrant.
     */
    [[nodiscard]] Result<std::reference_wrapper<AnimPose>> compose(const AnimPose& basePose);
    /** @brief Reclaim finished slots and invalidate their handles. */
    void collectFinished() noexcept;

private:
    struct Slot {
        std::unique_ptr<MontagePlayer> player;
        MontageHandle::generation_type generation = 1;
        bool                           retired    = false;
    };
    struct Layer {
        Slot                      slots[2];
        std::size_t               active = 1;
        std::unique_ptr<AnimPose> pose;
        std::unique_ptr<AnimPose>   rawPose;
        std::unique_ptr<AnimPose>   blendScratch;
        std::vector<float>        boneMask;
        float                       weight             = 1.0f;
        bool                        additive           = false;
        IMontageRootMotionReceiver* rootMotionReceiver = nullptr;
    };

    [[nodiscard]] static std::size_t handleIndex(std::size_t layer, std::size_t slot) noexcept {
        return layer * 2 + slot;
    }
    /** @brief Resolve an internal live slot. @return Borrowed owned slot or null. @lifetime Until next mutation. */
    [[nodiscard]] Slot* slotFor(MontageHandle handle) noexcept;
    /** @brief Resolve an internal live slot. @return Borrowed owned slot or null. @lifetime Until next mutation. */
    [[nodiscard]] const Slot* slotFor(MontageHandle handle) const noexcept;
    void                      ensureLayer(std::size_t layer);
    void                      retireSlot(Slot& slot) noexcept;
    [[nodiscard]] double      evaluateLayerPose(Layer& layer) const;

    AnimSkeleton&      skeleton_;
    std::vector<Layer> layers_;
    std::unique_ptr<AnimPose> composedPose_;
};

}  // namespace eve::animation
