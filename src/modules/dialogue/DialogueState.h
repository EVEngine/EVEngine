#pragma once

/**
 * @file DialogueState.h
 * @brief Dialogue's state boundary, condition adapter, and command request protocol.
 */

#include "common/StateAccess.h"
#include "common/StateValue.h"
#include "decision/Condition.h"
#include "dialogue/DialoguePayment.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace eve::dialogue {

/** @brief Destination protocol requested by a dialogue command node. */
enum class CommandRequestKind : std::uint8_t {
    Operation,
    GameplayAction,
};

/**
 * @brief Immutable request emitted by a command node.
 *
 * The request contains snapshots of the current frame's bindings and locals.
 * It never contains a world pointer and the runner does not retain a caller's
 * temporary arguments. Providers decide how to validate and execute the
 * operation/action in their own module.
 */
struct CommandRequest {
    std::string name;
    CommandRequestKind kind = CommandRequestKind::Operation;
    eve::Value arguments = eve::Value::Object{};
    eve::Value bindings = eve::Value::Object{};
    eve::Value locals = eve::Value::Object{};
    /** @brief Optional atomic money/reputation payment for this command. */
    PaymentSpec payment;
    /** @brief Optional StatePatch mutations committed in the same transaction. */
    std::vector<eve::StateMutation> stateMutations;
};

/** @brief Structured response to a request emitted by a command node. */
struct CommandResponse {
    enum class Status : std::uint8_t { Completed, Blocked, Failed };

    Status status = Status::Completed;
    eve::Value value;
    std::string error;
};

/** @brief Handler implemented by the owning operation or gameplay domain. */
using CommandRequestHandler = std::function<CommandResponse(const CommandRequest&)>;

/**
 * @brief Convert the legacy reload value into the canonical common value.
 * @param value Owned StateValue tree; it is not retained.
 * @return An owning common Value with identical JSON-compatible contents.
 */
[[nodiscard]] eve::Value toCanonicalValue(const eve::StateValue& value);

/**
 * @brief Convert a canonical value to the legacy reload representation.
 * @param value Owned common value; it is not retained.
 * @return An owning StateValue used by the existing persistence facade.
 */
[[nodiscard]] eve::StateValue toDialogueStateValue(const eve::Value& value);

/**
 * @brief Decision EvaluationContext backed by one dialogue subject.
 *
 * The explicit provider is borrowed for this context. When it does not know a
 * query, the context checks the registered `IStateQuery` capability/listeners.
 * A known false result is never confused with an unavailable provider.
 */
class StateEvaluationContext final : public eve::decision::EvaluationContext {
public:
    /** @brief Construct a context for a stable subject id and optional provider. */
    explicit StateEvaluationContext(std::string subject = {}, eve::IStateQuery* provider = nullptr);

    /** @brief Change the subject used by subsequent synchronous queries. */
    void setSubject(std::string subject);
    /** @brief Set a borrowed query provider valid for this context's lifetime. */
    void setProvider(eve::IStateQuery* provider) noexcept { provider_ = provider; }
    /** @brief Install a read-only policy callback without retaining its arguments. */
    void setPolicyEvaluator(
        std::function<std::optional<eve::decision::ConditionResult>(std::string_view, const eve::Value&)>
            evaluator) {
        policyEvaluator_ = std::move(evaluator);
    }

    /** @copydoc eve::decision::EvaluationContext::value */
    [[nodiscard]] std::optional<eve::Value> value(std::string_view key) const override;
    /** @copydoc eve::decision::EvaluationContext::hasTag */
    [[nodiscard]] std::optional<bool> hasTag(std::string_view tag) const override;
    /** @copydoc eve::decision::EvaluationContext::attribute */
    [[nodiscard]] std::optional<eve::Value> attribute(std::string_view key) const override;
    /** @copydoc eve::decision::EvaluationContext::resource */
    [[nodiscard]] std::optional<eve::Value> resource(std::string_view key) const override;
    /** @copydoc eve::decision::EvaluationContext::state */
    [[nodiscard]] std::optional<eve::Value> state(std::string_view key) const override;
    /** @copydoc eve::decision::EvaluationContext::authority */
    [[nodiscard]] std::optional<bool> authority(std::string_view scope) const override;
    /** @copydoc eve::decision::EvaluationContext::policy */
    [[nodiscard]] std::optional<eve::decision::ConditionResult> policy(
        std::string_view name, const eve::Value& arguments) const override;

private:
    std::string subject_;
    eve::IStateQuery* provider_ = nullptr;
    std::function<std::optional<eve::decision::ConditionResult>(std::string_view, const eve::Value&)>
        policyEvaluator_;
};

/**
 * @brief Dialogue-owned facade for world reads, unified conditions, and mutations.
 *
 * Dialogue owns only this borrowed boundary and never mirrors world flags,
 * attributes, reputation, or persistent values. Local conversation variables
 * remain in `ConversationRunner` frames. Mutation providers must implement the
 * all-or-nothing contract; persistent providers are expected to use
 * StatePatch's transaction participant internally.
 */
class DialogueStateContext {
public:
    /** @brief Construct a context for one stable world subject. */
    explicit DialogueStateContext(std::string subject = {});

    /** @brief Set the subject used for all future condition queries. */
    void setSubject(std::string subject);
    /** @brief Return the currently selected subject id. */
    const std::string& subject() const noexcept { return subject_; }
    /** @brief Bind a borrowed query provider for this context's lifetime. */
    void setQueryProvider(eve::IStateQuery* provider) noexcept { queryProvider_ = provider; }
    /** @brief Bind a borrowed mutation provider for this context's lifetime. */
    void setMutationProvider(eve::IStateMutation* provider) noexcept { mutationProvider_ = provider; }
    /** @brief Install a read-only policy evaluator used by PolicyCall conditions. */
    void setPolicyEvaluator(
        std::function<std::optional<eve::decision::ConditionResult>(std::string_view, const eve::Value&)>
            evaluator) {
        policyEvaluator_ = std::move(evaluator);
    }

    /** @brief Query a canonical world value without creating a local copy in Dialogue. */
    [[nodiscard]] std::optional<eve::Value> value(std::string_view key) const;
    /** @brief Query exact world tag membership. */
    [[nodiscard]] std::optional<bool> hasTag(std::string_view tag) const;
    /** @brief Query one attribute or reputation value. */
    [[nodiscard]] std::optional<eve::Value> attribute(std::string_view key) const;
    /** @brief Query one resource value. */
    [[nodiscard]] std::optional<eve::Value> resource(std::string_view key) const;
    /** @brief Query one persistent state value. */
    [[nodiscard]] std::optional<eve::Value> state(std::string_view key) const;

    /** @brief Compile a canonical condition specification to decision::Condition. */
    [[nodiscard]] eve::Result<eve::decision::Condition> compileCondition(
        const eve::Value& specification) const;
    /** @brief Evaluate an already compiled shared condition with explanation. */
    [[nodiscard]] eve::decision::ConditionResult evaluate(
        const eve::decision::Condition& condition) const;
    /** @brief Compile and evaluate a canonical condition specification. */
    [[nodiscard]] eve::decision::ConditionResult evaluate(const eve::Value& specification) const;

    /** @brief Apply a complete mutation set through one authoritative provider. */
    [[nodiscard]] eve::Result<eve::MutationReceipt> apply(
        std::span<const eve::StateMutation> mutations, const eve::MutationContext& context) const;

private:
    StateEvaluationContext evaluationContext() const;

    std::string subject_;
    eve::IStateQuery* queryProvider_ = nullptr;
    eve::IStateMutation* mutationProvider_ = nullptr;
    std::function<std::optional<eve::decision::ConditionResult>(std::string_view, const eve::Value&)>
        policyEvaluator_;
};

}  // namespace eve::dialogue
