#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "card/Card.h"
#include "card/CardContainers.h"
#include "common/Container.h"
#include "inventory/ContainerAdapters.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using eve::Revision;
using eve::container::Capacity;
using eve::container::ContainerId;
using eve::container::ContainerObject;
using eve::container::ContainerSnapshot;
using eve::container::IContainer;
using eve::container::Membership;
using eve::container::MembershipEntry;
using eve::container::MembershipId;
using eve::container::SlotIndex;

ContainerId  cid(const char* value) { return ContainerId(value); }
MembershipId mid(const char* value) { return MembershipId(value); }

/** @brief Failing second participant used to prove prepare has no half-state. */
class FailingPrepareContainer final : public IContainer {
public:
    explicit FailingPrepareContainer(ContainerId id) : descriptor_{std::move(id)} {}

    [[nodiscard]] const eve::container::ContainerDescriptor& descriptor() const noexcept override {
        return descriptor_;
    }
    [[nodiscard]] eve::Result<ContainerSnapshot> snapshot() const override {
        return eve::Result<ContainerSnapshot>::success(ContainerSnapshot{descriptor_.id, revision_, entries_});
    }
    [[nodiscard]] eve::Result<void> validateInsert(const ContainerObject&, std::optional<SlotIndex>,
                                                   std::optional<MembershipId>) const override {
        return eve::Result<void>::success();
    }
    [[nodiscard]] eve::Result<std::unique_ptr<IContainer::PreparedState>> prepare(const ContainerSnapshot&,
                                                                                  const ContainerSnapshot&) override {
        ++prepareCalls_;
        return eve::Result<std::unique_ptr<IContainer::PreparedState>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected destination prepare failure"));
    }

    [[nodiscard]] int prepareCalls() const noexcept { return prepareCalls_; }

private:
    eve::container::ContainerDescriptor descriptor_;
    Revision                            revision_ = Revision(0);
    std::vector<MembershipEntry>        entries_;
    int                                 prepareCalls_ = 0;
};

}  // namespace

TEST_CASE("container.zoneUsesStrongCoordinateSpacesAndValidatesShapes") {
    using namespace eve::container;
    static_assert(!std::is_convertible_v<Coordinate<ScreenSpace>, Coordinate<World2DSpace>>);
    static_assert(!std::is_convertible_v<Coordinate<World2DSpace>, Coordinate<World3DSpace>>);

    auto screen = Zone<ScreenSpace>::create(Rectangle<ScreenSpace>{{10.f, 20.f}, 100.f, 50.f}, Capacity::fixed(2));
    REQUIRE(screen.ok());
    CHECK(screen.value().contains({20.f, 30.f}));
    CHECK(!screen.value().contains({0.f, 0.f}));

    auto invalid2d = Zone<World2DSpace>::create(Rectangle<World2DSpace>{{0.f, 0.f}, -1.f, 2.f});
    CHECK(!invalid2d.ok());
    CHECK_EQ(invalid2d.code(), eve::StatusCode::Rejected);

    auto invalid3d = Zone<World3DSpace>::create(Box3D{{0.f, 0.f, 0.f}, 1.f, 2.f, -3.f});
    CHECK(!invalid3d.ok());

    auto sphere = Zone<World3DSpace>::create(Sphere3D{{0.f, 0.f, 0.f}, 2.f});
    REQUIRE(sphere.ok());
    CHECK(sphere.value().contains({1.f, 1.f, 1.f}));
    CHECK(!sphere.value().contains({2.f, 2.f, 2.f}));
}

TEST_CASE("container.cardDeckHandDiscardTransferEmitsOneStrongEvent") {
    eve::card::Card card;
    REQUIRE_EQ(card.registerCardsFromJson(R"([{"id":"creature","kind":"creature"},{"id":"spell","kind":"spell"}])"), 2);
    auto* deck = card.newDeck();
    auto* hand = card.newHand(card.newConfig());
    REQUIRE(deck != nullptr);
    REQUIRE(hand != nullptr);
    auto* creature = card.newCard("creature");
    REQUIRE(creature != nullptr);
    deck->push(creature);

    eve::card::CardContainerAdapter source(cid("card:deck"), eve::card::CardContainerKind::Deck, deck);
    eve::card::CardContainerAdapter destination(cid("card:hand"), eve::card::CardContainerKind::Hand, nullptr, hand,
                                                nullptr, Capacity::fixed(2));
    std::vector<eve::container::TransferEvent> events;
    eve::container::TransferRequest            request{
        &source,           &destination,          mid(creature->identity()->id.c_str()), SlotIndex(0), SlotIndex(0),
        source.revision(), destination.revision()};
    auto transferred = eve::container::TransferService::transfer(
        request, [&](const eve::container::TransferEvent& event) { events.push_back(event); });
    REQUIRE(transferred.ok());
    CHECK_EQ(deck->count(), 0);
    CHECK_EQ(hand->count(), 1);
    CHECK_EQ(events.size(), std::size_t(1));
    CHECK_EQ(events.front().source.value(), std::string("card:deck"));
    CHECK_EQ(events.front().destination.value(), std::string("card:hand"));

    std::vector<eve::card::CardData*> discard;
    eve::card::CardContainerAdapter discardAdapter(cid("card:discard"), eve::card::CardContainerKind::Discard, nullptr,
                                                   nullptr, &discard);
    auto                            toDiscard = eve::container::TransferService::transfer(
        eve::container::TransferRequest{&destination, &discardAdapter, mid(creature->identity()->id.c_str()),
                                        SlotIndex(0), std::nullopt, destination.revision(), discardAdapter.revision()},
        [](const eve::container::TransferEvent&) { throw std::runtime_error("observer failure"); });
    REQUIRE(toDiscard.ok());
    CHECK_EQ(toDiscard.code(), eve::StatusCode::Applied);
    CHECK(toDiscard.status().hasDiagnostics());
    REQUIRE(toDiscard.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(toDiscard.status().primaryDiagnostic()->code(), eve::DiagnosticCode::CallbackFailure);
    CHECK_EQ(hand->count(), 0);
    CHECK_EQ(discard.size(), std::size_t(1));
}

TEST_CASE("container.cardTransferRejectsFilterAndDuplicateWithoutMutation") {
    eve::card::Card card;
    REQUIRE_EQ(card.registerCardsFromJson(R"([{"id":"creature","kind":"creature"},{"id":"spell","kind":"spell"}])"), 2);
    auto* deck     = card.newDeck();
    auto* hand     = card.newHand(card.newConfig());
    auto* creature = card.newCard("creature");
    REQUIRE(deck != nullptr);
    REQUIRE(hand != nullptr);
    REQUIRE(creature != nullptr);
    deck->push(creature);
    eve::card::CardContainerAdapter source(cid("card:deck-filter"), eve::card::CardContainerKind::Deck, deck);
    eve::card::CardContainerAdapter destination(cid("card:spell-hand"), eve::card::CardContainerKind::Hand, nullptr,
                                                hand, nullptr, Capacity::fixed(2), {"spell"});
    auto                            rejected = eve::container::TransferService::transfer(
        eve::container::TransferRequest{&source, &destination, mid(creature->identity()->id.c_str()), SlotIndex(0),
                                        SlotIndex(0), source.revision(), destination.revision()});
    CHECK(!rejected.ok());
    CHECK_EQ(deck->count(), 1);
    CHECK_EQ(hand->count(), 0);

    hand->addCard(creature);
    eve::card::CardContainerAdapter unrestrictedDestination(
        cid("card:duplicate-hand"), eve::card::CardContainerKind::Hand, nullptr, hand, nullptr, Capacity::fixed(2));
    auto duplicate = eve::container::TransferService::transfer(eve::container::TransferRequest{
        &source, &unrestrictedDestination, mid(creature->identity()->id.c_str()), SlotIndex(0), SlotIndex(0),
        source.revision(), unrestrictedDestination.revision()});
    CHECK(!duplicate.ok());
    CHECK_EQ(deck->count(), 1);
    CHECK_EQ(hand->count(), 1);
}

TEST_CASE("container.transferRestoresSourceAfterApplyFailure") {
    eve::card::Card card;
    REQUIRE_EQ(card.registerCardsFromJson(R"([{"id":"creature","kind":"creature"}])"), 1);
    auto* deck     = card.newDeck();
    auto* creature = card.newCard("creature");
    REQUIRE(deck != nullptr);
    REQUIRE(creature != nullptr);
    deck->push(creature);
    eve::card::CardContainerAdapter source(cid("card:rollback"), eve::card::CardContainerKind::Deck, deck);
    FailingPrepareContainer         destination(cid("failing:destination"));
    auto                            failed = eve::container::TransferService::transfer(
        eve::container::TransferRequest{&source, &destination, mid(creature->identity()->id.c_str()), SlotIndex(0),
                                        std::nullopt, source.revision(), destination.snapshot().value().revision});
    CHECK(!failed.ok());
    CHECK_EQ(destination.prepareCalls(), 1);
    CHECK_EQ(deck->count(), 1);
    CHECK_EQ(deck->peek(), creature);
    auto destinationAfter = destination.snapshot();
    REQUIRE(destinationAfter.ok());
    CHECK(destinationAfter.value().entries.empty());
}

TEST_CASE("container.inventoryBagAndEquipmentTransferRejectsStaleAndFits") {
    using namespace eve::inventory;
    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    ItemDefinition sword;
    sword.id        = "sword";
    sword.maxStack  = 1;
    sword.tags      = {"weapon"};
    sword.equipSlot = "mainhand";
    ItemRegistry::registerItem(sword);

    Bag       bag(2);
    const int added = bag.addItem("sword", 1);
    REQUIRE_EQ(added, 1);
    EquipmentSet equipment;
    equipment.defineSlot("mainhand");
    InventoryContainerAdapter bagAdapter(cid("inventory:bag"), &bag);
    InventoryContainerAdapter equipmentAdapter(cid("inventory:equipment"), &equipment);
    const auto                oldRevision = bagAdapter.revision();
    const auto                object      = MembershipId("inventory:" + std::to_string(bag.getSlotInstanceId(0)));

    auto equipped = eve::container::TransferService::transfer(eve::container::TransferRequest{
        &bagAdapter, &equipmentAdapter, object, SlotIndex(0), SlotIndex(0), oldRevision, equipmentAdapter.revision()});
    REQUIRE(equipped.ok());
    CHECK(bag.isSlotEmpty(0));
    CHECK_EQ(equipment.getSlotItemId("mainhand"), std::string("sword"));

    auto stale = eve::container::TransferService::transfer(eve::container::TransferRequest{
        &bagAdapter, &equipmentAdapter, object, SlotIndex(0), SlotIndex(0), oldRevision, equipmentAdapter.revision()});
    CHECK(!stale.ok());
    CHECK_EQ(stale.code(), eve::StatusCode::Rejected);

    auto unequipped = eve::container::TransferService::transfer(
        eve::container::TransferRequest{&equipmentAdapter, &bagAdapter, object, SlotIndex(0), SlotIndex(1),
                                        equipmentAdapter.revision(), bagAdapter.revision()});
    REQUIRE(unequipped.ok());
    CHECK_EQ(bag.getSlotInstanceId(1), std::stoi(object.value().substr(std::string("inventory:").size())));
    CHECK(equipment.isSlotEmpty("mainhand"));

    ItemRegistry::clear();
}

TEST_CASE("container.sameContainerMoveUsesFinalSlotAndRejectsOccupiedSlot") {
    using namespace eve::inventory;
    ItemRegistry::clear();
    InventorySystem::ensureBuiltins();
    ItemDefinition item;
    item.id       = "token";
    item.maxStack = 1;
    ItemRegistry::registerItem(item);
    Bag       bag(3);
    const int firstAdded  = bag.addItem("token", 1);
    const int secondAdded = bag.addItem("token", 1);
    REQUIRE_EQ(firstAdded, 1);
    REQUIRE_EQ(secondAdded, 1);
    InventoryContainerAdapter adapter(cid("inventory:same"), &bag);
    const auto                first    = MembershipId("inventory:" + std::to_string(bag.getSlotInstanceId(0)));
    const auto                revision = adapter.revision();
    auto                      moved    = eve::container::TransferService::transfer(
        eve::container::TransferRequest{&adapter, &adapter, first, SlotIndex(0), SlotIndex(2), revision, revision});
    REQUIRE(moved.ok());
    CHECK_EQ(bag.getSlotInstanceId(2), std::stoi(first.value().substr(10)));
    CHECK(bag.isSlotEmpty(0));
    const auto second            = MembershipId("inventory:" + std::to_string(bag.getSlotInstanceId(1)));
    const auto afterMoveRevision = adapter.revision();
    auto       occupied          = eve::container::TransferService::transfer(eve::container::TransferRequest{
        &adapter, &adapter, second, SlotIndex(1), SlotIndex(2), afterMoveRevision, afterMoveRevision});
    CHECK(!occupied.ok());
    CHECK_EQ(bag.getSlotInstanceId(2), std::stoi(first.value().substr(10)));
    ItemRegistry::clear();
}
