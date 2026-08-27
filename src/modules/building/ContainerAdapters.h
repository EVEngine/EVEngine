#pragma once

#include "common/Time.h"

/**
 * @file ContainerAdapters.h
 * @brief Building adapters for the common Container/Zone/Transfer protocol.
 */

#include "building/PlacementWorld.h"
#include "common/Container.h"

namespace eve::building {

/** @brief Owning payload carrying a garrison member's domain facts. */
struct BuildingGarrisonContainerObject final : eve::container::ContainerObjectPayload {
    GarrisonMember member;
};

/**
 * @brief Transactional adapter over one placed building's authoritative garrison.
 *
 * The `PlacementWorld` and building instance are borrowed; neither is deleted
 * by this adapter. Membership is stored in `PlacedBuilding::garrison`, so a
 * second adapter observes the same state and a rebuilt adapter does not lose
 * members. The building must remain present until a prepared state is
 * committed or rolled back. Removal/replacement is stale-safe at prepare time;
 * violating the lifetime contract before noexcept commit terminates.
 *
 * Garrison members are insertion-ordered and use contiguous slots. Capacity
 * and the side-effect-free accepted condition are supplied by the caller,
 * because building definitions do not own a universal population policy.
 */
class BuildingGarrisonContainerAdapter final : public eve::container::IContainer {
public:
    /**
     * @brief Binds an adapter to a placed building.
     * @param id Stable identity, normally `building:garrison:<world>:<instance>`.
     * @param world Borrowed placement world.
     * @param instanceId Existing placed-building instance identity.
     * @param capacity Maximum member count.
     * @param accepted Side-effect-free condition applied before entry.
     */
    BuildingGarrisonContainerAdapter(
        eve::container::ContainerId id, PlacementWorld* world, int instanceId,
        eve::container::Capacity capacity = eve::container::Capacity::unlimited(),
        eve::container::AcceptedCondition accepted = {});

    [[nodiscard]] const eve::container::ContainerDescriptor& descriptor() const noexcept override {
        return descriptor_;
    }
    [[nodiscard]] eve::Result<eve::container::ContainerSnapshot> snapshot() const override;
    [[nodiscard]] eve::Result<void> validateInsert(
        const eve::container::ContainerObject& object,
        std::optional<eve::container::SlotIndex> destination,
        std::optional<eve::container::MembershipId> ignoredObject = std::nullopt) const override;
    /** @copydoc eve::container::IContainer::prepare */
    [[nodiscard]] eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>> prepare(
        const eve::container::ContainerSnapshot& expected,
        const eve::container::ContainerSnapshot& candidate) override;

    /** @brief Return the authoritative garrison revision, or zero when stale. */
    [[nodiscard]] eve::Revision revision() const noexcept;

    /**
     * @brief Stage and publish a new garrison member.
     * @param memberId Stable member identity; it must be non-empty.
     * @param type Domain type spelling retained in snapshots.
     * @param tags Member tags used by accepted conditions and consumers.
     * @param sink Optional synchronous canonical GameEvent observer.
     * @param tick Injected simulation tick for emitted envelopes.
     * @return The member identity, or a rejection with no membership mutation.
     */
    [[nodiscard]] eve::Result<eve::container::MembershipId> enter(
        std::string memberId, std::string type = "building.garrison.member",
        std::vector<std::string> tags = {}, eve::container::GameEventSink sink = {},
        eve::SimulationTick tick = eve::SimulationTick::zero());

    /**
     * @brief Stage and publish removal of one member.
     * @param memberId Stable member identity.
     * @param sink Optional synchronous canonical GameEvent observer.
     * @param tick Injected simulation tick for emitted envelopes.
     * @return The removed identity, or a rejection with no mutation.
     */
    [[nodiscard]] eve::Result<eve::container::MembershipId> exit(
        eve::container::MembershipId memberId, eve::container::GameEventSink sink = {},
        eve::SimulationTick tick = eve::SimulationTick::zero());

private:
    class PreparedState;

    /**
     * @brief Resolve the borrowed building currently owned by the placement world.
     * @ownership Borrowed from `world_`; this adapter never deletes it.
     * @nullable Null when the world is unbound or the instance is absent.
     * @lifetime Valid only until the next world structural mutation or world
     *            destruction; callers must use it only during this synchronous call.
     * @thread Placement-world owner thread only; no concurrent access is provided.
     * @reentrancy No callbacks are made and re-entry is not required.
     */
    [[nodiscard]] const PlacedBuilding* building() const noexcept;
    /** @copydoc BuildingGarrisonContainerAdapter::building() */
    [[nodiscard]] PlacedBuilding* building() noexcept;
    [[nodiscard]] std::uint64_t nextEventSerial() noexcept;
    [[nodiscard]] static eve::container::ContainerObject describe(const GarrisonMember& member);

    eve::container::ContainerDescriptor descriptor_;
    PlacementWorld* world_ = nullptr;
    int instanceId_ = 0;
    std::uint64_t eventSerial_ = 0;
};

}  // namespace eve::building
