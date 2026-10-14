#include "card/Card.h"
#include "card/CardOwnerInternal.h"
namespace eve::card {
using card_internal::ownsCard;
eve::Result<eve::transaction::TransactionReceipt> Card::play(CardData&                        card,
                                                             eve::resource::IResourceAccount& playerAccount,
                                                             CardPlayComposition              composition,
                                                             std::string                      transactionId) {
    if (!ownsCard(cards_, card))
        return eve::Result<eve::transaction::TransactionReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "card does not belong to this Card facade", "card"));
    const auto* definition = findDef(card.identity()->definitionId);
    if (definition == nullptr)
        return eve::Result<eve::transaction::TransactionReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "card definition was not found", "definition"));
    CardPlayRequest request;
    request.card          = &card;
    request.definition    = definition;
    request.playerAccount = &playerAccount;
    request.composition   = std::move(composition);
    request.transactionId = std::move(transactionId);
    return CardPlayPaymentAdapter::play(std::move(request));
}

Result<std::optional<resource::CostSpec>> Card::playCost(CardData& card) const {
    if (!ownsCard(cards_, card))
        return Result<std::optional<resource::CostSpec>>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "card is not owned by this facade", "card"));
    const auto* definition = findDef(card.identity()->definitionId);
    if (!definition)
        return Result<std::optional<resource::CostSpec>>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "card definition is absent", "card.definition"));
    return CardPlayPaymentAdapter::manaCost(*definition);
}

namespace {
class HandConsumption final : public transaction::ITransactionParticipant {
public:
    HandConsumption(Hand& hand, CardData& card) : hand_(ecs::handle_of(&hand)), card_(ecs::handle_of(&card)) {}
    std::string_view name() const noexcept override { return "card-hand-consumption"; }
    Result<void>     prepare(const transaction::TransactionContext&) override {
        if (prepared_ || committed_) return failure("hand stage is already in flight");
        auto* hand = resolveHand();
        auto* card = resolveCard();
        if (!hand || !card) return failure("hand or card is stale");
        const auto& values = hand->membership()->cards;
        if (std::count(values.begin(), values.end(), card) != 1)
            return failure("card membership is absent or duplicated");
        before_     = values;
        dragBefore_ = *hand->drag();
        prepared_   = true;
        return Result<void>::success();
    }
    Result<void> commit(const transaction::TransactionContext&) override {
        if (!prepared_ || committed_) return failure("hand stage is not prepared");
        auto* hand = resolveHand();
        auto* card = resolveCard();
        if (!hand || !card) return failure("hand or card is stale");
        if (hand->membership()->cards != before_) return failure("hand changed before commit");
        auto& values = hand->membership()->cards;
        values.erase(std::find(values.begin(), values.end(), card));
        if (hand->drag()->card == card) hand->drag()->card = nullptr;
        if (hand->drag()->press == card) hand->drag()->press = nullptr;
        committed_ = true;
        return Result<void>::success();
    }
    Result<void> rollback(const transaction::TransactionContext&) override {
        if (!prepared_ || committed_) return failure("hand stage is not prepared");
        prepared_ = false;
        return Result<void>::success();
    }
    Result<void> compensate(const transaction::TransactionContext&) override {
        if (!committed_) return failure("hand stage is not committed");
        auto* hand = resolveHand();
        auto* card = resolveCard();
        if (!hand || !card) return failure("hand or card is stale");
        auto expected = before_;
        expected.erase(std::find(expected.begin(), expected.end(), card));
        if (hand->membership()->cards != expected) return failure("hand changed before compensation");
        hand->membership()->cards.swap(before_);
        *hand->drag() = dragBefore_;
        committed_    = false;
        prepared_     = false;
        return Result<void>::success();
    }

private:
    static Result<void> failure(std::string message) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict, std::move(message), "card.hand"));
    }
    Hand* resolveHand() const {
        return hand_.table == ecs::current() ? dynamic_cast<Hand*>(ecs::try_get(hand_)) : nullptr;
    }
    CardData* resolveCard() const {
        return card_.table == ecs::current() ? dynamic_cast<CardData*>(ecs::try_get(card_)) : nullptr;
    }
    ecs::EntityHandle      hand_, card_;
    std::vector<CardData*> before_;
    Hand::Drag             dragBefore_;
    bool                   prepared_ = false, committed_ = false;
};
}  // namespace
Result<std::unique_ptr<transaction::ITransactionParticipant>> Card::prepareHandConsumption(CardData& card) const {
    auto cost = playCost(card);
    if (!cost) return Result<std::unique_ptr<transaction::ITransactionParticipant>>::failure(cost.status());
    Hand* found = nullptr;
    for (const auto& handle : hands_) {
        auto* hand = dynamic_cast<Hand*>(ecs::try_get(handle));
        if (!hand) continue;
        if (std::find(hand->membership()->cards.begin(), hand->membership()->cards.end(), &card) ==
            hand->membership()->cards.end())
            continue;
        if (found)
            return Result<std::unique_ptr<transaction::ITransactionParticipant>>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "card is in multiple hands", "card.hand"));
        found = hand;
    }
    if (!found)
        return Result<std::unique_ptr<transaction::ITransactionParticipant>>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "card is not in a hand", "card.hand"));
    return Result<std::unique_ptr<transaction::ITransactionParticipant>>::success(
        std::make_unique<HandConsumption>(*found, card));
}
}  // namespace eve::card
