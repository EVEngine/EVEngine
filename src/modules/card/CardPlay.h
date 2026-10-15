#pragma once
#include "common/Export.h"


/**
 * @file CardPlay.h
 * @brief Card Play adapter for the renderer-independent decision protocol.
 */

#include "card/CardTypes.h"
#include "common/Container.h"
#include "condition/Condition.h"
#include "transaction/AtomicResourcePayment.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eve::card {

/**
 * @brief Read-only extension points used while evaluating a card play.
 *
 * Providers are called synchronously on the caller's thread. They must not
 * mutate card state, retain the arguments, or call unknown code while holding
 * a lock. An empty provider means that capability is unavailable.
 */
struct CardPlayConditionQueries {
    using Resource  = std::function<std::optional<eve::Value>(std::string_view key)>;
    using Authority = std::function<std::optional<bool>(std::string_view scope)>;
    using Policy =
        std::function<std::optional<condition::ConditionResult>(std::string_view name, const eve::Value& arguments)>;

    Resource  resource;
    Authority authority;
    Policy    policy;
};

/**
 * @brief Read-only EvaluationContext backed by one card instance and definition.
 *
 * Both pointers are borrowed only for the synchronous `evaluate` call. Card
 * attributes expose the definition's cost/attack/health, while resource,
 * authority and policy lookups are supplied by the owning game/profile.
 */
class CardPlayConditionContext final : public condition::EvaluationContext {
public:
    /**
     * @brief Bind a card instance and definition for one evaluation.
     * @param card Borrowed card instance; it must outlive the evaluation call.
     * @param definition Borrowed definition; it must outlive the call.
     * @param queries Optional read-only providers for external capabilities.
     */
    CardPlayConditionContext(const CardData* card, const CardDefinition& definition,
                             CardPlayConditionQueries queries = {});

    /** @copydoc condition::EvaluationContext::value */
    /** @brief Value. */
    [[nodiscard]] std::optional<eve::Value> value(std::string_view key) const override;
    /** @copydoc condition::EvaluationContext::policy */
    /** @brief Policy. */
    [[nodiscard]] std::optional<condition::ConditionResult> policy(std::string_view  name,
                                                                  const eve::Value& arguments) const override;

private:
    const CardData*          card_       = nullptr;
    const CardDefinition*    definition_ = nullptr;
    CardPlayConditionQueries queries_;
};

/** @brief Evaluates one card play condition without moving or mutating a card. */
class EVENGINE_API_WORLD CardPlayConditionAdapter {
public:
    /**
     * @brief Evaluate a card definition's play condition against one instance.
     * @param card Borrowed card instance observed synchronously; may be null.
     * @param definition Borrowed definition observed synchronously.
     * @param condition Owning, side-effect-free condition tree.
     * @param queries Optional read-only resource, authority and policy providers.
     * @return A stable explanation suitable for UI rejection text.
     */
    [[nodiscard]] static condition::ConditionResult evaluate(const CardData* card, const CardDefinition& definition,
                                                            const condition::Condition& condition,
                                                            CardPlayConditionQueries   queries = {});
};

/**
 * @brief Transaction participant that publishes one CardData play.
 *
 * The card state remains owned by CardData.  This participant only stages the
 * phase transition and restores it on rollback/compensation; it never owns a
 * card or an account.  The card is borrowed for the synchronous transaction
 * call and must remain live on the owner thread.
 */
class EVENGINE_API_WORLD CardPlayParticipant final : public eve::transaction::ITransactionParticipant {
public:
    /** @brief Bind one card effect whose state transition is played. */
    explicit CardPlayParticipant(CardData& card);
    CardPlayParticipant(const CardPlayParticipant&)            = delete;
    CardPlayParticipant& operator=(const CardPlayParticipant&) = delete;
    CardPlayParticipant(CardPlayParticipant&&)                 = delete;
    CardPlayParticipant& operator=(CardPlayParticipant&&)      = delete;

    /** @brief Stable transaction diagnostic name. */
    [[nodiscard]] std::string_view name() const noexcept override { return "card-play-effect"; }
    /** @copydoc eve::transaction::ITransactionParticipant::prepare */
    /** @brief Prepare. */
    [[nodiscard]] eve::Result<void> prepare(const eve::transaction::TransactionContext& context) override;
    /** @copydoc eve::transaction::ITransactionParticipant::commit */
    /** @brief Commits . */
    [[nodiscard]] eve::Result<void> commit(const eve::transaction::TransactionContext& context) override;
    /** @copydoc eve::transaction::ITransactionParticipant::rollback */
    /** @brief Rollback. */
    [[nodiscard]] eve::Result<void> rollback(const eve::transaction::TransactionContext& context) override;
    /** @copydoc eve::transaction::ITransactionParticipant::compensate */
    /** @brief Compensate. */
    [[nodiscard]] eve::Result<void> compensate(const eve::transaction::TransactionContext& context) override;

private:
    ecs::EntityHandle card_;
    /** @brief Resolve the generation handle before a lifecycle mutation.
     * @ownership Borrowed entity owned by its Card facade/ECS table.
     * @lifetime Valid only through the synchronous lifecycle call.
     * @nullable Returns null after entity destruction or table replacement.
     * @thread Simulation owner thread; no callbacks or reentry.
     */
    CardData* resolve() const;
    CardState previousState_ = CardState::Hand;
    bool      prepared_      = false;
    bool      committed_     = false;
};

/**
 * @brief Optional cross-domain operations that belong to one card play.
 *
 * The source and destination containers are borrowed and must be distinct.
 * The transfer is staged through their public container contracts and is
 * committed by the same Coordinator as card state, effect and Mana. The
 * supplied effect is optional for cards whose only observable effect is the
 * card state transition.
 */
struct CardPlayComposition {
    CardPlayConditionQueries                   conditionQueries;
    eve::container::IContainer*                source      = nullptr;
    eve::container::IContainer*                destination = nullptr;
    std::optional<eve::container::SlotIndex>   sourceSlot;
    std::optional<eve::container::SlotIndex>   destinationSlot;
    eve::transaction::ITransactionParticipant* effect = nullptr;
    /**
     * @brief Optional owning ruleset cost replacing definition Mana; empty uses Mana.
     * @remarks An engaged invalid CostSpec is rejected, never treated as free.
     * This is a per-play input and does not change the registered definition.
     */
    std::optional<eve::resource::CostSpec> paymentCost;
    /**
     * @brief Additional borrowed effects committed after effect in listed order.
     * @remarks Participants must be unique, non-null and live for the synchronous
     * owner-thread call. Callbacks must not reenter play on the same card/account.
     */
    std::vector<eve::transaction::ITransactionParticipant*> additionalEffects;
};

/**
 * @brief Complete caller-owned input for one atomic card play.
 *
 * Pointers are borrowed only for the synchronous call. The composition root
 * supplies the authoritative account and optional container/effect ports;
 * this type does not perform implicit player lookup.
 */
struct CardPlayRequest {
    CardData*                        card          = nullptr;
    const CardDefinition*            definition    = nullptr;
    eve::resource::IResourceAccount* playerAccount = nullptr;
    CardPlayComposition              composition;
    std::string                      transactionId;
};

/**
 * @brief Connects a card's mana field to the owning player's resource account.
 *
 * `CardDefinition::cost` is only a domain definition value.  The player's
 * account is supplied by the composition root, and the payment is routed
 * through AtomicResourcePayment, so card state and mana are one transaction.
 */
class EVENGINE_API_WORLD CardPlayPaymentAdapter final {
public:
    /**
     * @brief Execute condition, movement, card effect and Mana as one transaction.
     * @param request Borrowed composition request; all pointed-to objects must
     *        remain live until the Result is returned.
     * @return A committed receipt, or a failure with all prepared/committed
     *         participants rolled back or compensated.
     * @remarks This is the preferred Card entry point. Condition evaluation is
     *          pure preflight; it cannot debit or move a card. No caller-side
     *          participant assembly is required.
     */
    [[nodiscard]] static eve::Result<eve::transaction::TransactionReceipt> play(CardPlayRequest request);

    /**
     * @brief Convert a non-negative card mana value into a canonical cost.
     * @param definition Borrowed card definition.
     * @return A mana CostSpec, or an empty optional for a free card.
     */
    [[nodiscard]] static eve::Result<std::optional<eve::resource::CostSpec>> manaCost(const CardDefinition& definition);

    /**
     * @brief Play one card and charge its player's mana atomically.
     * @param card Borrowed CardData whose authoritative state becomes Played.
     * @param definition Borrowed definition supplying the mana cost.
     * @param playerAccount Borrowed account owned by the player/profile.
     * @param transactionId Non-empty correlation id; empty derives from definition id.
     * @return Committed transaction receipt or structured failure. A failed
     *         condition/effect/payment leaves both card and account unchanged.
     */
    [[nodiscard]] static eve::Result<eve::transaction::TransactionReceipt> play(
        CardData& card, const CardDefinition& definition, eve::resource::IResourceAccount& playerAccount,
        std::string transactionId = {});
};

}  // namespace eve::card
