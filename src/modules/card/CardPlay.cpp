#include "card/CardPlay.h"

#include <algorithm>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace eve::card {
namespace {

std::optional<eve::Value> cardValue(const CardData* card, const CardDefinition& definition,
                                    std::string_view key) {
    if (!card) return std::nullopt;
    const auto state = const_cast<CardData*>(card)->state();
    if (key == "card.id") return eve::Value(const_cast<CardData*>(card)->identity()->id);
    if (key == "card.definition") return eve::Value(definition.id);
    if (key == "card.kind") return eve::Value(definition.kind);
    if (key == "card.cost") return eve::Value(definition.cost);
    if (key == "card.attack") return eve::Value(definition.attack);
    if (key == "card.health") return eve::Value(definition.health);
    if (key == "card.phase") return eve::Value(cardStateName(state->phase));
    return std::nullopt;
}

}  // namespace

CardPlayConditionContext::CardPlayConditionContext(const CardData* card, const CardDefinition& definition,
                                                   CardPlayConditionQueries queries)
    : card_(card), definition_(&definition), queries_(std::move(queries)) {}

std::optional<eve::Value> CardPlayConditionContext::value(std::string_view key) const {
    return cardValue(card_, *definition_, key);
}

std::optional<bool> CardPlayConditionContext::hasTag(std::string_view tag) const {
    if (!card_) return std::nullopt;
    return std::find(definition_->tags.begin(), definition_->tags.end(), tag) != definition_->tags.end();
}

std::optional<eve::Value> CardPlayConditionContext::attribute(std::string_view key) const {
    if (!card_) return std::nullopt;
    if (key == "cost" || key == "card.cost") return eve::Value(definition_->cost);
    if (key == "attack" || key == "card.attack") return eve::Value(definition_->attack);
    if (key == "health" || key == "card.health") return eve::Value(definition_->health);
    return std::nullopt;
}

std::optional<eve::Value> CardPlayConditionContext::resource(std::string_view key) const {
    if (!queries_.resource) return std::nullopt;
    return queries_.resource(key);
}

std::optional<eve::Value> CardPlayConditionContext::state(std::string_view key) const {
    return cardValue(card_, *definition_, key);
}

std::optional<bool> CardPlayConditionContext::authority(std::string_view scope) const {
    if (!queries_.authority) return std::nullopt;
    return queries_.authority(scope);
}

std::optional<decision::ConditionResult> CardPlayConditionContext::policy(std::string_view name,
                                                                            const eve::Value& arguments) const {
    if (!queries_.policy) return std::nullopt;
    return queries_.policy(name, arguments);
}

decision::ConditionResult CardPlayConditionAdapter::evaluate(const CardData* card,
                                                              const CardDefinition& definition,
                                                              const decision::Condition& condition,
                                                              CardPlayConditionQueries queries) {
    CardPlayConditionContext context(card, definition, std::move(queries));
    return condition.evaluate(context);
}

namespace {

template <class T>
eve::Result<T> cardFailure(eve::DiagnosticCode code, std::string message,
                           std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

eve::Result<void> cardLifecycleConflict(std::string message) {
    return eve::Result<void>::failure(eve::Status::failure(
        eve::StatusCode::Conflict,
        eve::Diagnostic::error(eve::DiagnosticCode::Conflict, std::move(message), "card.play")));
}

class CardTransferParticipant final : public eve::transaction::ITransactionParticipant {
public:
    CardTransferParticipant(CardData& card, eve::container::IContainer& source,
                            eve::container::IContainer& destination,
                            std::optional<eve::container::SlotIndex> sourceSlot,
                            std::optional<eve::container::SlotIndex> destinationSlot)
        : card_(card), source_(source), destination_(destination),
          sourceSlot_(sourceSlot), destinationSlot_(destinationSlot) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return "card-container-transfer";
    }

    [[nodiscard]] eve::Result<void> prepare(
        const eve::transaction::TransactionContext&) override {
        if (prepared_ || committed_)
            return cardLifecycleConflict("card transfer participant is already in flight");
        if (&source_ == &destination_)
            return cardFailure<void>(eve::DiagnosticCode::Conflict,
                                     "card play source and destination must differ");

        auto sourceResult = source_.snapshot();
        if (!sourceResult) return eve::Result<void>::failure(sourceResult.status());
        auto destinationResult = destination_.snapshot();
        if (!destinationResult) return eve::Result<void>::failure(destinationResult.status());
        beforeSource_ = std::move(sourceResult).takeValue();
        beforeDestination_ = std::move(destinationResult).takeValue();

        const eve::container::MembershipId object(card_.identity()->id);
        const auto sourceIt = std::find_if(
            beforeSource_.entries.begin(), beforeSource_.entries.end(),
            [&object](const auto& entry) { return entry.membership.object == object; });
        if (sourceIt == beforeSource_.entries.end())
            return cardFailure<void>(eve::DiagnosticCode::NotFound,
                                     "card is not present in the source container", "source");
        if (sourceSlot_ && *sourceSlot_ != sourceIt->membership.slot)
            return cardFailure<void>(eve::DiagnosticCode::StaleHandle,
                                     "card source slot is stale", "sourceSlot");

        auto accepted = destination_.validateInsert(
            sourceIt->object, destinationSlot_, std::nullopt);
        if (!accepted) return accepted;
        if (destinationSlot_ &&
            (destinationSlot_->value() < 0 ||
             static_cast<std::size_t>(destinationSlot_->value()) >
                 beforeDestination_.entries.size()))
            return cardFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                     "card destination slot is out of range", "destinationSlot");

        afterSource_ = beforeSource_;
        afterSource_.entries.erase(
            afterSource_.entries.begin() + (sourceIt - beforeSource_.entries.begin()));
        renumber(afterSource_);
        afterSource_.revision = nextRevision(beforeSource_.revision);
        if (afterSource_.revision == beforeSource_.revision)
            return cardFailure<void>(eve::DiagnosticCode::InvariantViolation,
                                     "source container revision is exhausted");

        afterDestination_ = beforeDestination_;
        auto moved = *sourceIt;
        const std::size_t insertion = destinationSlot_
            ? static_cast<std::size_t>(destinationSlot_->value())
            : afterDestination_.entries.size();
        if (insertion > afterDestination_.entries.size())
            return cardFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                     "card destination slot is out of range", "destinationSlot");
        afterDestination_.entries.insert(afterDestination_.entries.begin() + insertion,
                                         std::move(moved));
        renumber(afterDestination_);
        afterDestination_.revision = nextRevision(beforeDestination_.revision);
        if (afterDestination_.revision == beforeDestination_.revision)
            return cardFailure<void>(eve::DiagnosticCode::InvariantViolation,
                                     "destination container revision is exhausted");

        auto sourcePrepared = source_.prepare(beforeSource_, afterSource_);
        if (!sourcePrepared) return eve::Result<void>::failure(sourcePrepared.status());
        sourceStage_ = std::move(sourcePrepared).takeValue();
        if (!sourceStage_)
            return cardFailure<void>(eve::DiagnosticCode::InvariantViolation,
                                     "source container returned an empty stage");
        auto destinationPrepared = destination_.prepare(beforeDestination_, afterDestination_);
        if (!destinationPrepared) {
            sourceStage_->rollback();
            sourceStage_.reset();
            return eve::Result<void>::failure(destinationPrepared.status());
        }
        destinationStage_ = std::move(destinationPrepared).takeValue();
        if (!destinationStage_) {
            sourceStage_->rollback();
            sourceStage_.reset();
            return cardFailure<void>(eve::DiagnosticCode::InvariantViolation,
                                     "destination container returned an empty stage");
        }
        prepared_ = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> commit(
        const eve::transaction::TransactionContext&) override {
        if (!prepared_ || committed_)
            return cardLifecycleConflict("card transfer has no prepared stage");
        destinationStage_->commit();
        sourceStage_->commit();
        committed_ = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> rollback(
        const eve::transaction::TransactionContext&) override {
        if (committed_)
            return cardLifecycleConflict("committed card transfer requires compensation");
        if (destinationStage_) destinationStage_->rollback();
        if (sourceStage_) sourceStage_->rollback();
        sourceStage_.reset();
        destinationStage_.reset();
        prepared_ = false;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> compensate(
        const eve::transaction::TransactionContext&) override {
        if (!committed_)
            return cardLifecycleConflict("card transfer has no committed state to compensate");

        auto currentSource = source_.snapshot();
        if (!currentSource) return eve::Result<void>::failure(currentSource.status());
        auto currentDestination = destination_.snapshot();
        if (!currentDestination) return eve::Result<void>::failure(currentDestination.status());
        if (currentSource.value().revision != afterSource_.revision ||
            currentDestination.value().revision != afterDestination_.revision)
            return cardFailure<void>(eve::DiagnosticCode::StaleHandle,
                                     "card containers changed before compensation");
        auto sourceCandidate = beforeSource_;
        auto destinationCandidate = beforeDestination_;
        sourceCandidate.revision = nextRevision(currentSource.value().revision);
        destinationCandidate.revision = nextRevision(currentDestination.value().revision);
        if (sourceCandidate.revision == currentSource.value().revision ||
            destinationCandidate.revision == currentDestination.value().revision)
            return cardFailure<void>(eve::DiagnosticCode::InvariantViolation,
                                     "card transfer compensation revision is exhausted");

        auto sourceRestore = source_.prepare(currentSource.value(), sourceCandidate);
        if (!sourceRestore) return eve::Result<void>::failure(sourceRestore.status());
        auto sourceStage = std::move(sourceRestore).takeValue();
        if (!sourceStage)
            return cardFailure<void>(eve::DiagnosticCode::InvariantViolation,
                                     "source compensation returned an empty stage");
        auto destinationRestore = destination_.prepare(currentDestination.value(), destinationCandidate);
        if (!destinationRestore) {
            sourceStage->rollback();
            return eve::Result<void>::failure(destinationRestore.status());
        }
        auto destinationStage = std::move(destinationRestore).takeValue();
        if (!destinationStage) {
            sourceStage->rollback();
            return cardFailure<void>(eve::DiagnosticCode::InvariantViolation,
                                     "destination compensation returned an empty stage");
        }
        destinationStage->commit();
        sourceStage->commit();
        sourceStage_.reset();
        destinationStage_.reset();
        prepared_ = false;
        committed_ = false;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

private:
    static eve::Revision nextRevision(eve::Revision revision) noexcept {
        const auto next = revision.incremented();
        return next ? *next : revision;
    }

    static void renumber(eve::container::ContainerSnapshot& snapshot) noexcept {
        for (std::size_t index = 0; index < snapshot.entries.size(); ++index)
            snapshot.entries[index].membership.slot =
                eve::container::SlotIndex(static_cast<std::int32_t>(index));
    }

    CardData& card_;
    eve::container::IContainer& source_;
    eve::container::IContainer& destination_;
    std::optional<eve::container::SlotIndex> sourceSlot_;
    std::optional<eve::container::SlotIndex> destinationSlot_;
    eve::container::ContainerSnapshot beforeSource_;
    eve::container::ContainerSnapshot beforeDestination_;
    eve::container::ContainerSnapshot afterSource_;
    eve::container::ContainerSnapshot afterDestination_;
    std::unique_ptr<eve::container::IContainer::PreparedState> sourceStage_;
    std::unique_ptr<eve::container::IContainer::PreparedState> destinationStage_;
    bool prepared_ = false;
    bool committed_ = false;
};

}  // namespace

CardPlayParticipant::CardPlayParticipant(CardData& card) : card_(card) {}

eve::Result<void> CardPlayParticipant::prepare(
    const eve::transaction::TransactionContext& context) {
    (void)context;
    if (prepared_ || committed_)
        return cardLifecycleConflict("card play participant is already in flight or terminal");
    auto state = card_.state();
    if (state->phase == CardState::Played || state->phase == CardState::Discarded ||
        state->phase == CardState::Disabled)
        return cardLifecycleConflict("card is not playable in its current state");
    previousState_ = state->phase;
    prepared_ = true;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> CardPlayParticipant::commit(
    const eve::transaction::TransactionContext& context) {
    (void)context;
    if (!prepared_ || committed_)
        return cardLifecycleConflict("card play participant has no uncommitted stage");
    auto state = card_.state();
    // A preceding container-transfer participant deliberately projects the
    // card's destination phase during the same synchronous transaction. The
    // play participant is the final authority and publishes Played once all
    // preceding participants have committed successfully.
    state->phase = CardState::Played;
    committed_ = true;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> CardPlayParticipant::rollback(
    const eve::transaction::TransactionContext& context) {
    (void)context;
    if (!prepared_ || committed_)
        return cardLifecycleConflict("card play participant has no prepared stage to roll back");
    prepared_ = false;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> CardPlayParticipant::compensate(
    const eve::transaction::TransactionContext& context) {
    (void)context;
    if (!committed_)
        return cardLifecycleConflict("card play participant has no committed effect to compensate");
    auto state = card_.state();
    state->phase = previousState_;
    committed_ = false;
    prepared_ = false;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<std::optional<eve::resource::CostSpec>> CardPlayPaymentAdapter::manaCost(
    const CardDefinition& definition) {
    if (definition.id.empty())
        return cardFailure<std::optional<eve::resource::CostSpec>>(
            eve::DiagnosticCode::InvalidArgument,
            "card definition id must not be empty", "definition.id");
    if (definition.cost < 0)
        return cardFailure<std::optional<eve::resource::CostSpec>>(
            eve::DiagnosticCode::InvalidArgument,
            "card mana cost must not be negative", "definition.cost");
    if (definition.cost == 0)
        return eve::Result<std::optional<eve::resource::CostSpec>>::success(std::nullopt);
    auto cost = eve::resource::CostSpec::single("mana", definition.cost);
    if (!cost)
        return eve::Result<std::optional<eve::resource::CostSpec>>::failure(cost.status());
    return eve::Result<std::optional<eve::resource::CostSpec>>::success(
        std::move(cost).takeValue());
}

eve::Result<eve::transaction::TransactionReceipt> CardPlayPaymentAdapter::play(
    CardData& card, const CardDefinition& definition,
    eve::resource::IResourceAccount& playerAccount, std::string transactionId) {
    CardPlayRequest request;
    request.card = &card;
    request.definition = &definition;
    request.playerAccount = &playerAccount;
    request.transactionId = std::move(transactionId);
    return play(std::move(request));
}

eve::Result<eve::transaction::TransactionReceipt> CardPlayPaymentAdapter::play(
    CardPlayRequest request) {
    if (request.card == nullptr || request.definition == nullptr)
        return cardFailure<eve::transaction::TransactionReceipt>(
            eve::DiagnosticCode::InvalidArgument,
            "card play requires a card and definition", "request");
    auto& card = *request.card;
    const auto& definition = *request.definition;
    if (request.composition.source == nullptr !=
        (request.composition.destination == nullptr))
        return cardFailure<eve::transaction::TransactionReceipt>(
            eve::DiagnosticCode::InvalidArgument,
            "card play requires both source and destination containers", "composition");

    const auto condition = CardPlayConditionAdapter::evaluate(
        &card, definition, definition.playCondition,
        request.composition.conditionQueries);
    if (!condition.passed()) {
        DiagnosticDetails details;
        details.emplace_back("reason", decision::conditionReasonCodeName(condition.reasonCode()));
        return eve::Result<eve::transaction::TransactionReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                   "card play condition was rejected", "condition",
                                   std::move(details)));
    }

    auto cost = manaCost(definition);
    if (!cost) return eve::Result<eve::transaction::TransactionReceipt>::failure(cost.status());
    if (cost.value() && request.playerAccount == nullptr)
        return cardFailure<eve::transaction::TransactionReceipt>(
            eve::DiagnosticCode::InvalidArgument,
            "a mana-bearing card play requires a player account", "playerAccount");
    if (request.transactionId.empty()) request.transactionId = "card.play." + definition.id;

    CardPlayParticipant state(card);
    std::unique_ptr<CardTransferParticipant> transfer;
    std::vector<eve::transaction::ITransactionParticipant*> participants;
    participants.reserve(request.composition.effect ? 3u : 2u);
    if (request.composition.source != nullptr) {
        transfer = std::make_unique<CardTransferParticipant>(
            card, *request.composition.source, *request.composition.destination,
            request.composition.sourceSlot, request.composition.destinationSlot);
        participants.push_back(transfer.get());
    }
    participants.push_back(&state);
    if (request.composition.effect != nullptr) participants.push_back(request.composition.effect);

    eve::transaction::TransactionContext context(std::move(request.transactionId));
    if (cost.value())
        return eve::transaction::AtomicResourcePayment::execute(
            context, *request.playerAccount, *cost.value(),
            std::span<eve::transaction::ITransactionParticipant*>(participants));
    return eve::transaction::AtomicResourcePayment::execute(
        context, std::span<eve::transaction::ITransactionParticipant*>(participants));
}

}  // namespace eve::card
