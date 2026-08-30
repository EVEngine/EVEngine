#pragma once

/**
 * @file CardContainers.h
 * @brief Container protocol adapters for card deck, hand and discard piles.
 *
 * Draw and shuffle remain on Deck. This adapter only exposes membership
 * transfer; presentation layout, hover and drag are intentionally outside the
 * common container contract.
 */

#include "card/CardTypes.h"
#include "common/Container.h"

#include <string>
#include <vector>

namespace eve::card {

enum class CardContainerKind : std::uint8_t { Deck, Hand, Discard };

/**
 * @brief Generation-qualified reference to one CardData object.
 *
 * A snapshot owns this payload, but never owns the card. The ECS handle is
 * intentionally stored instead of a raw pointer so a snapshot crossing a
 * call boundary can detect card destruction or slot reuse before it is used.
 */
struct CardContainerObject final : eve::container::ContainerObjectPayload {
    ecs::EntityHandle card;
};

/**
 * @brief Exposes one card collection through the generic atomic transfer API.
 *
 * The referenced Deck/Hand/vector is caller-owned and must outlive every
 * synchronous call. The adapter does not delete cards. For Discard, the
 * caller supplies the authoritative vector because Card's existing discard
 * UI is deliberately not made a second source of truth. The legacy vectors
 * still contain borrowed pointers, so the owning Card module must remove a
 * card from all such vectors before destroying its ECS entity.
 *
 * Snapshots carry an ECS handle and therefore reject destroyed or reused card
 * slots as stale. Restore must recreate Card entities first, then construct a
 * fresh adapter over the restored collection.
 */
class CardContainerAdapter final : public eve::container::IContainer {
public:
    /**
     * @brief Bind a borrowed Deck, Hand or discard collection.
     * @param id Stable container identity.
     * @param kind Which Card collection is exposed.
     * @param deck Borrowed deck when kind is Deck.
     * @param hand Borrowed hand when kind is Hand.
     * @param discard Borrowed authoritative vector when kind is Discard.
     * @param capacity Optional maximum number of cards.
     * @param acceptedKinds Optional card-kind allowlist.
     */
    CardContainerAdapter(eve::container::ContainerId id, CardContainerKind kind, Deck* deck = nullptr,
                         Hand* hand = nullptr, std::vector<CardData*>* discard = nullptr,
                         eve::container::Capacity capacity      = eve::container::Capacity::unlimited(),
                         std::vector<std::string> acceptedKinds = {});

    [[nodiscard]] const eve::container::ContainerDescriptor& descriptor() const noexcept override {
        return descriptor_;
    }
    [[nodiscard]] eve::Result<eve::container::ContainerSnapshot> snapshot() const override;
    [[nodiscard]] eve::Result<void>                              validateInsert(
                                     const eve::container::ContainerObject& object, std::optional<eve::container::SlotIndex> destination,
                                     std::optional<eve::container::MembershipId> ignoredObject = std::nullopt) const override;
    /** @copydoc eve::container::IContainer::prepare */
    [[nodiscard]] eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>> prepare(
        const eve::container::ContainerSnapshot& expected, const eve::container::ContainerSnapshot& candidate) override;

    /** @brief Return the current adapter revision for optimistic requests. */
    [[nodiscard]] eve::Revision revision() const noexcept { return revision_; }

private:
    class PreparedState;

    /**
     * @brief Resolve the authoritative card vector for one synchronous adapter operation.
     * @ownership Borrowed from the bound Deck, Hand, or discard owner; never delete it.
     * @nullable Null when the selected owner was not bound.
     * @lifetime The returned vector and its element pointers are valid only while
     *            the bound owner remains alive; vector contents can be invalidated
     *            by a membership mutation, so this pointer is never retained.
     * @thread Bound card-owner thread only; external synchronization is required.
     * @reentrancy No callback is made while the pointer is held.
     */
    [[nodiscard]] std::vector<CardData*>* cards() noexcept;
    /** @copydoc CardContainerAdapter::cards() */
    [[nodiscard]] const std::vector<CardData*>*          cards() const noexcept;
    [[nodiscard]] static eve::container::MembershipId    objectId(const CardData& card);
    [[nodiscard]] static eve::container::ContainerObject describe(CardData* card);
    void                                                 updateState(CardData* card) const noexcept;

    eve::container::ContainerDescriptor descriptor_;
    CardContainerKind                   kind_;
    Deck*                               deck_     = nullptr;
    Hand*                               hand_     = nullptr;
    std::vector<CardData*>*             discard_  = nullptr;
    eve::Revision                       revision_ = eve::Revision(0);
};

}  // namespace eve::card
