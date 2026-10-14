#include <simplesquirrel/simplesquirrel.hpp>
#include "card/Card.h"
#include "card/CardOwnerInternal.h"
#include "common/SquirrelBinding.h"
#include "transaction/AtomicBatch.h"

namespace eve::card {
namespace {
class PlayRules final : public transaction::ITransactionParticipant {
public:
    PlayRules(CardData& card, CardDefinition definition, transaction::AtomicBatch& batch)
        : card_(ecs::handle_of(&card)), definition_(std::move(definition)), batch_(batch) {}
    Result<void> prepare(const transaction::TransactionContext&) override {
        auto* card = card_.table == ecs::current() ? dynamic_cast<CardData*>(ecs::try_get(card_)) : nullptr;
        if (!card || card->visual()->disabled) return failure("card is stale or disabled");
        const auto phase = card->state()->phase;
        if (phase != CardState::Hand && phase != CardState::Hovered && phase != CardState::Dragging &&
            phase != CardState::Returning)
            return failure("card is not in a playable hand phase");
        CardPlayConditionQueries queries;
        queries.resource = [this](std::string_view name) { return batch_.resource(name); };
        if (!CardPlayConditionAdapter::evaluate(card, definition_, definition_.playCondition, std::move(queries))
                 .passed())
            return failure("card play condition was rejected");
        return Result<void>::success();
    }
    Result<void> commit(const transaction::TransactionContext&) override { return Result<void>::success(); }
    Result<void> rollback(const transaction::TransactionContext&) override { return Result<void>::success(); }
    Result<void> compensate(const transaction::TransactionContext&) override { return Result<void>::success(); }

private:
    static Result<void> failure(std::string text) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, std::move(text), "card"));
    }
    ecs::EntityHandle         card_;
    CardDefinition            definition_;
    transaction::AtomicBatch& batch_;
};
}  // namespace
Result<void> Card::stagePlay(transaction::AtomicBatch& batch, CardData& card) const {
    auto cost = playCost(card);
    if (!cost) return batch.reject(cost.status());
    auto hand = prepareHandConsumption(card);
    if (!hand) return batch.reject(hand.status());
    std::vector<std::unique_ptr<transaction::ITransactionParticipant>> effects;
    effects.push_back(std::make_unique<PlayRules>(card, *findDef(card.identity()->definitionId), batch));
    effects.push_back(std::make_unique<CardPlayParticipant>(card));
    effects.push_back(std::move(hand).takeValue());
    return batch.add(std::move(effects), std::move(cost).takeValue());
}
namespace card_internal {
void exposeStageBindings(ssq::Class& cls) {
    const auto vm = cls.getHandle();
    cls.addFunc("stagePlay", [vm](Card* self, transaction::AtomicBatch* batch, CardData* card) {
        auto retained = batch->retainScriptOwner(vm);
        if (!retained) return script::projectResult(vm, std::move(retained));
        return script::projectResult(vm, self->stagePlay(*batch, *card));
    });
}
}  // namespace card_internal
}  // namespace eve::card
