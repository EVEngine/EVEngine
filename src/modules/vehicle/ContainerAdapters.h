#pragma once

#include "common/Time.h"

/**
 * @file ContainerAdapters.h
 * @brief Vehicle adapters for the common Container/Zone/Transfer protocol.
 *
 * Seats remain a Vehicle component and the adapter is only a transactional
 * view over that component. It does not introduce a gameplay root or move
 * driving/camera policy into common.
 */

#include "common/Container.h"
#include "vehicle/VehicleTypes.h"

#include <string>

namespace eve::vehicle {

/** @brief Owning snapshot payload for a vehicle occupant membership. */
struct VehicleSeatContainerObject final : eve::container::ContainerObjectPayload {
    int occupantId = 0;
    std::string driver;
};

/**
 * @brief Transactional adapter over one VehicleEntity::Seats component.
 *
 * The ECS entity is held by a generation-qualified handle. The adapter does
 * not own the vehicle or occupants. A prepared state must be committed or
 * rolled back before the vehicle entity is destroyed; stale destruction is
 * rejected during snapshot/prepare and an invalid lifetime during noexcept
 * commit terminates, as required by `IContainer::PreparedState`.
 *
 * Seat membership is explicit-slot, capacity equals the number of declared
 * seats, and the seat component's revision is the sole optimistic-concurrency
 * revision. VehicleSystem's legacy enter/exit facade delegates to this class.
 */
class VehicleSeatContainerAdapter final : public eve::container::IContainer {
public:
    /**
     * @brief Binds an adapter to a live ECS vehicle.
     * @param id Stable container identity, normally `vehicle:seat:<vehicle-id>`.
     * @param vehicle Borrowed VehicleEntity; the ECS handle detects destruction
     *        and slot reuse after construction.
     * @param accepted Optional side-effect-free object filter.
     */
    VehicleSeatContainerAdapter(eve::container::ContainerId id, VehicleEntity* vehicle,
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

    /** @brief Return the live seat-component revision, or zero when stale. */
    [[nodiscard]] eve::Revision revision() const noexcept;

    /**
     * @brief Stage and publish an occupant entering a seat.
     * @param seatIndex Explicit seat slot.
     * @param occupantId Positive stable gameplay occupant ID.
     * @param sink Optional synchronous canonical GameEvent observer.
     * @param tick Injected simulation tick for emitted envelopes.
     * @return Occupant membership identity, or a rejection with no mutation.
     */
    [[nodiscard]] eve::Result<eve::container::MembershipId> enter(
        eve::container::SlotIndex seatIndex, int occupantId,
        eve::container::GameEventSink sink = {},
        eve::SimulationTick tick = eve::SimulationTick::zero());

    /**
     * @brief Stage and publish the occupant leaving a seat.
     * @param seatIndex Explicit seat slot.
     * @param sink Optional synchronous canonical GameEvent observer.
     * @param tick Injected simulation tick for emitted envelopes.
     * @return Removed occupant identity, or a rejection with no mutation.
     */
    [[nodiscard]] eve::Result<eve::container::MembershipId> exit(
        eve::container::SlotIndex seatIndex, eve::container::GameEventSink sink = {},
        eve::SimulationTick tick = eve::SimulationTick::zero());

private:
    class PreparedState;

    [[nodiscard]] VehicleEntity* vehicle() const noexcept;
    [[nodiscard]] std::uint64_t nextEventSerial() noexcept;
    [[nodiscard]] static eve::container::MembershipId occupantId(int occupantId);
    [[nodiscard]] static eve::container::ContainerObject describe(
        int occupantId, std::string driver = {});

    eve::container::ContainerDescriptor descriptor_;
    ecs::EntityHandle vehicle_;
    std::uint64_t eventSerial_ = 0;
};

}  // namespace eve::vehicle
