#include "card/CardContainers.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <unordered_set>

namespace eve::card {
namespace {

[[nodiscard]] eve::Result<void> error(eve::DiagnosticCode code, std::string message) {
    return eve::Result<void>::failure(eve::Diagnostic::error(code, std::move(message)));
}

[[nodiscard]] bool hasId(const std::vector<CardData*>& cards, const eve::container::MembershipId& id) {
    return std::any_of(cards.begin(), cards.end(), [&](const CardData* card) {
        return card != nullptr && const_cast<CardData*>(card)->identity()->id == id.value();
    });
}

[[nodiscard]] bool sameLayout(const eve::container::ContainerSnapshot& lhs,
                              const eve::container::ContainerSnapshot& rhs) {
    if (lhs.id != rhs.id || lhs.revision != rhs.revision || lhs.entries.size() != rhs.entries.size()) return false;
    for (std::size_t index = 0; index < lhs.entries.size(); ++index) {
        const auto& left  = lhs.entries[index];
        const auto& right = rhs.entries[index];
        if (left.membership.object != right.membership.object || left.membership.slot != right.membership.slot ||
            left.membership.generation != right.membership.generation || left.object.id != right.object.id ||
            left.object.type != right.object.type || left.object.quantity != right.object.quantity)
            return false;
    }
    return true;
}

}  // namespace

class CardContainerAdapter::PreparedState final : public eve::container::IContainer::PreparedState {
public:
    PreparedState(CardContainerAdapter& owner, std::vector<CardData*> staged, eve::Revision revision)
        : owner_(owner), staged_(std::move(staged)), revision_(revision) {}

    void commit() noexcept override {
        if (committed_) return;
        auto* live = owner_.cards();
        if (live == nullptr) std::terminate();
        live->swap(staged_);
        owner_.revision_ = revision_;
        for (auto* card : *live) owner_.updateState(card);
        committed_ = true;
    }

    void rollback() noexcept override {
        if (!committed_) staged_.clear();
    }

private:
    CardContainerAdapter&  owner_;
    std::vector<CardData*> staged_;
    eve::Revision          revision_;
    bool                   committed_ = false;
};

CardContainerAdapter::CardContainerAdapter(eve::container::ContainerId id, CardContainerKind kind, Deck* deck,
                                           Hand* hand, std::vector<CardData*>* discard,
                                           eve::container::Capacity capacity, std::vector<std::string> acceptedKinds)
    : descriptor_{std::move(id), capacity,
                  kind == CardContainerKind::Deck ? eve::container::Ordering::Stack
                                                  : eve::container::Ordering::Insertion,
                  eve::container::Filter{}},
      kind_(kind),
      deck_(deck),
      hand_(hand),
      discard_(discard) {
    if (!acceptedKinds.empty()) {
        descriptor_.filter = eve::container::Filter(
            [acceptedKinds = std::move(acceptedKinds)](const eve::container::ContainerObject& object) {
                if (std::find(acceptedKinds.begin(), acceptedKinds.end(), object.type) == acceptedKinds.end())
                    return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                             "card kind is not accepted by container"));
                return eve::Result<void>::success();
            });
    }
    if (kind_ == CardContainerKind::Deck && deck_ == nullptr)
        descriptor_.filter = eve::container::Filter([](const eve::container::ContainerObject&) {
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "card deck adapter has no Deck"));
        });
    if (kind_ == CardContainerKind::Hand && hand_ == nullptr)
        descriptor_.filter = eve::container::Filter([](const eve::container::ContainerObject&) {
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "card hand adapter has no Hand"));
        });
    if (kind_ == CardContainerKind::Discard && discard_ == nullptr)
        descriptor_.filter = eve::container::Filter([](const eve::container::ContainerObject&) {
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "card discard adapter has no storage"));
        });
}

std::vector<CardData*>* CardContainerAdapter::cards() noexcept {
    if (kind_ == CardContainerKind::Deck) return deck_ ? &deck_->membership()->cards : nullptr;
    if (kind_ == CardContainerKind::Hand) return hand_ ? &hand_->membership()->cards : nullptr;
    return discard_;
}

const std::vector<CardData*>* CardContainerAdapter::cards() const noexcept {
    if (kind_ == CardContainerKind::Deck) return deck_ ? &deck_->membership()->cards : nullptr;
    if (kind_ == CardContainerKind::Hand) return hand_ ? &hand_->membership()->cards : nullptr;
    return discard_;
}

eve::container::MembershipId CardContainerAdapter::objectId(const CardData& card) {
    return eve::container::MembershipId(const_cast<CardData&>(card).identity()->id);
}

eve::container::ContainerObject CardContainerAdapter::describe(CardData* card) {
    eve::container::ContainerObject object;
    if (card == nullptr) return object;
    object.id       = objectId(*card);
    object.type     = card->identity()->kind;
    object.quantity = 1;
    auto payload    = std::make_shared<CardContainerObject>();
    payload->card   = ecs::handle_of(card);
    object.payload  = std::move(payload);
    return object;
}

void CardContainerAdapter::updateState(CardData* card) const noexcept {
    if (card == nullptr) return;
    if (kind_ == CardContainerKind::Deck)
        card->state()->phase = CardState::Deck;
    else if (kind_ == CardContainerKind::Hand)
        card->state()->phase = card->visual()->disabled ? CardState::Disabled : CardState::Hand;
    else
        card->state()->phase = CardState::Discarded;
}

eve::Result<eve::container::ContainerSnapshot> CardContainerAdapter::snapshot() const {
    const auto* values = cards();
    if (!descriptor_.id.isValid() || values == nullptr)
        return eve::Result<eve::container::ContainerSnapshot>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "card container adapter is not bound"));
    if (!descriptor_.capacity.isUnlimited() && values->size() > descriptor_.capacity.value())
        return eve::Result<eve::container::ContainerSnapshot>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "card container exceeds configured capacity"));
    eve::container::ContainerSnapshot                snapshot{descriptor_.id, revision_, {}};
    std::unordered_set<eve::container::MembershipId> ids;
    snapshot.entries.reserve(values->size());
    for (std::size_t i = 0; i < values->size(); ++i) {
        if (i > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            return eve::Result<eve::container::ContainerSnapshot>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvariantViolation, "card container has too many indexed memberships"));
        CardData* card = (*values)[i];
        if (card == nullptr || card->identity()->id.empty())
            return eve::Result<eve::container::ContainerSnapshot>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvariantViolation, "card container contains null or unidentified card"));
        const auto id = objectId(*card);
        if (!ids.insert(id).second)
            return eve::Result<eve::container::ContainerSnapshot>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "card container contains duplicate card identity"));
        snapshot.entries.push_back(
            {{id, eve::container::SlotIndex(static_cast<std::int32_t>(i)), eve::Generation(1)}, describe(card)});
    }
    return eve::Result<eve::container::ContainerSnapshot>::success(std::move(snapshot));
}

eve::Result<void> CardContainerAdapter::validateInsert(
    const eve::container::ContainerObject& object, std::optional<eve::container::SlotIndex> destination,
    std::optional<eve::container::MembershipId> ignoredObject) const {
    const auto* values = cards();
    if (values == nullptr || !descriptor_.id.isValid())
        return error(eve::DiagnosticCode::InvalidArgument, "card container adapter is not bound");
    if (!object.id.isValid() || !object.payload)
        return error(eve::DiagnosticCode::InvalidArgument, "card transfer object is incomplete");
    if (object.quantity != 1u)
        return error(eve::DiagnosticCode::InvalidArgument, "a card container membership must have quantity one");
    const auto* payload = dynamic_cast<const CardContainerObject*>(object.payload.get());
    auto*       card    = payload == nullptr ? nullptr : static_cast<CardData*>(ecs::try_get(payload->card));
    if (payload == nullptr || card == nullptr || card->identity()->id != object.id.value() ||
        card->identity()->kind != object.type)
        return error(eve::DiagnosticCode::StaleHandle, "card transfer payload is stale");
    if (hasId(*values, object.id) && (!ignoredObject || *ignoredObject != object.id))
        return error(eve::DiagnosticCode::Conflict, "destination already contains card");
    const std::size_t effectiveSize = values->size() - ((ignoredObject && hasId(*values, *ignoredObject)) ? 1u : 0u);
    if (destination) {
        if (!destination->isValid() || static_cast<std::size_t>(destination->value()) > effectiveSize)
            return error(eve::DiagnosticCode::InvalidArgument, "destination card slot is out of range");
    }
    auto accepted = descriptor_.filter.evaluate(object);
    if (!accepted) return accepted;
    if (!descriptor_.capacity.isUnlimited() && effectiveSize >= descriptor_.capacity.value())
        return error(eve::DiagnosticCode::Conflict, "card container capacity is full");
    return eve::Result<void>::success();
}

eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>> CardContainerAdapter::prepare(
    const eve::container::ContainerSnapshot& expected, const eve::container::ContainerSnapshot& candidate) {
    const auto* values = cards();
    if (values == nullptr || !descriptor_.id.isValid())
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "card container adapter is not bound"));
    if (expected.id != descriptor_.id || candidate.id != descriptor_.id)
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "card prepare targets another container"));
    auto currentResult = snapshot();
    if (!currentResult)
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(currentResult.status());
    if (!sameLayout(currentResult.value(), expected))
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "card prepare expected snapshot is stale"));
    const auto nextRevision = expected.revision.incremented();
    if (!nextRevision || candidate.revision != *nextRevision)
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "card candidate revision is not the next revision"));
    if (!descriptor_.capacity.isUnlimited() && candidate.entries.size() > descriptor_.capacity.value())
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "card candidate exceeds container capacity"));

    std::vector<const eve::container::MembershipEntry*> ordered;
    ordered.reserve(candidate.entries.size());
    for (const auto& entry : candidate.entries) ordered.push_back(&entry);
    std::sort(ordered.begin(), ordered.end(),
              [](const auto* lhs, const auto* rhs) { return lhs->membership.slot < rhs->membership.slot; });
    std::vector<CardData*> staged;
    staged.reserve(ordered.size());
    std::unordered_set<eve::container::MembershipId> ids;
    for (std::size_t index = 0; index < ordered.size(); ++index) {
        const auto* entry = ordered[index];
        if (!entry->membership.object.isValid() || !entry->membership.slot.isValid() ||
            entry->membership.slot.value() != static_cast<std::int32_t>(index) ||
            entry->membership.generation.isZero() || entry->object.id != entry->membership.object ||
            entry->object.quantity != 1u || !ids.insert(entry->membership.object).second)
            return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                       "card candidate contains invalid membership"));
        const auto* payload = dynamic_cast<const CardContainerObject*>(entry->object.payload.get());
        auto*       card    = payload == nullptr ? nullptr : static_cast<CardData*>(ecs::try_get(payload->card));
        if (payload == nullptr || card == nullptr || card->identity()->id != entry->membership.object.value() ||
            entry->object.type != card->identity()->kind)
            return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "card candidate payload is stale"));
        staged.push_back(card);
    }
    std::unique_ptr<eve::container::IContainer::PreparedState> prepared =
        std::make_unique<PreparedState>(*this, std::move(staged), *nextRevision);
    return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::success(std::move(prepared));
}

}  // namespace eve::card
