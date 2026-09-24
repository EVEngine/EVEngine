#pragma once
#include "common/Export.h"


/** @file StepKindRegistry.h @brief Extensible step vocabulary, payload schema and dispatch. */

#include "common/Result.h"
#include "common/Value.h"
#include "dnut_interpreter/SequenceAsset.h"

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace eve::dnut {

/** @brief Declares whether a step completes inside its call or suspends the runtime. */
enum class StepShape : std::uint8_t {
    /** @brief The step completes synchronously; `Completed` continues at `next`. */
    Instant,
    /**
     * @brief The step may suspend the runtime.
     *
     * A registered handler returning `Blocked` suspends until the host calls
     * `SequenceRuntime::resumeStep`. A descriptor with no handler is a
     * host-presented step: the runtime suspends and the host acknowledges with
     * `SequenceRuntime::advance`.
     */
    Await,
};

/** @brief Accepted value kind of one declared payload field. */
enum class StepFieldType : std::uint8_t { String, Number, Integer, Boolean, Any };

/** @brief One declared payload field of a step type. */
struct StepField {
    std::string   name;
    StepFieldType type     = StepFieldType::String;
    bool          required  = false;
};

/** @brief Owning authoring/runtime contract for one step type. */
struct StepKindDescriptor {
    std::string            type;
    std::string            displayName;
    std::string            category;
    StepShape              shape = StepShape::Instant;
    std::vector<StepField> fields;
    /**
     * @brief Optional domain invariant applied after the field schema passes.
     *
     * The field schema can express presence and value kind; a domain rule that
     * spans several fields (for example "exactly one of `learn` / `forget`")
     * belongs here so it is checked when content is compiled rather than when a
     * player reaches the step. Returning a failure rejects the authored asset.
     */
    std::function<eve::Result<void>(const SequenceNode& node)> validate;
};

/** @brief Terminal state of one dispatched step. */
enum class StepStatus : std::uint8_t { Completed, Blocked, Failed };

/** @brief Structured outcome of one dispatched step. */
struct StepOutcome {
    StepStatus  status = StepStatus::Completed;
    std::string error;
    eve::Value  value;
};

/**
 * @brief Borrowed execution context handed to one step handler.
 *
 * Every member is borrowed for the duration of the synchronous handler call and
 * must not be retained. Handlers run on the runtime's owner thread and must not
 * re-enter the runtime that dispatched them.
 */
struct StepContext {
    std::string       assetId;
    std::string       nodeId;
    const eve::Value* bindings = nullptr;
    const eve::Value* locals   = nullptr;
    /**
     * @brief Opaque consumer context installed by the runtime owner.
     *
     * The language core never reads this pointer. A consumer that registers
     * handlers uses it to reach its own per-run binding without making the core
     * depend on that consumer's types. Its lifetime belongs to whoever called
     * `SequenceRuntime::setHostContext`.
     */
    void* host = nullptr;
};

/** @brief Consumer-owned handler for one registered step type. */
using StepHandler = std::function<StepOutcome(const SequenceNode&, const StepContext&)>;

/**
 * @brief Canonical step vocabulary owner and runtime handler router.
 *
 * Descriptors are the single source of truth for a step type's payload fields:
 * the compiler validates authored steps against them and authoring tools derive
 * their field lists from them, so no second field table exists.
 *
 * All methods are owner-thread-only. Handlers are invoked synchronously without
 * locks and must not retain their arguments.
 */
class EVENGINE_API_PLATFORM StepKindRegistry {
public:
    /**
     * @brief Declare one step type.
     * @param descriptor Owning contract; `type` must be a non-empty bare word.
     * @return Success, or a structured failure for an empty/duplicate type.
     * @thread Owner thread only; no synchronization is performed.
     * @reentrancy Invokes no callbacks.
     */
    [[nodiscard]] eve::Result<void> registerStep(StepKindDescriptor descriptor);

    /**
     * @brief Bind an owning handler to an already declared step type.
     * @return Success, or a structured failure when the type is not declared.
     * @remarks An `Instant` type must have a handler before it can be validated;
     *          an `Await` type may remain host-presented and unhandled.
     */
    [[nodiscard]] eve::Result<void> registerHandler(std::string_view type, StepHandler handler);

    /** @brief Remove a step type and its handler; absent types are a no-op. */
    void unregisterStep(std::string_view type);

    /** @brief Return whether `type` is declared. */
    [[nodiscard]] bool contains(std::string_view type) const;

    /**
     * @brief Return the declared contract for `type`.
     * @return Borrowed pointer owned by this registry; nullptr when absent.
     * @ownership Borrowed. @nullable Yes.
     * @lifetime Invalidated by registerStep or unregisterStep of the same type.
     */
    [[nodiscard]] const StepKindDescriptor* descriptor(std::string_view type) const;

    /** @brief Return every declared contract in lexical type order. */
    [[nodiscard]] std::vector<StepKindDescriptor> descriptors() const;

    /** @brief Return whether a handler is bound for `type`. */
    [[nodiscard]] bool hasHandler(std::string_view type) const;

    /**
     * @brief Validate one node against its declared payload schema.
     *
     * Rejects an unknown step type, a non-object payload, a missing or
     * ill-typed required field, and any undeclared field. An `Instant` type
     * without a bound handler is rejected because it could never run.
     *
     * @return Success, or a structured diagnostic naming the violating field.
     * @thread Owner thread only.
     */
    [[nodiscard]] eve::Result<void> validate(const SequenceNode& node) const;

    /**
     * @brief Route one already validated node to its handler.
     * @param node Node to execute; its payload is NOT re-validated here.
     * @param context Borrowed execution context valid only for this call.
     * @return The handler outcome. A host-presented `Await` type (declared
     *         without a handler) returns `Blocked`; an unknown or unhandled
     *         `Instant` type returns `Failed` rather than a silent success.
     * @thread Owner thread only; the handler runs synchronously.
     * @reentrancy Handlers must not re-enter the dispatching runtime.
     */
    [[nodiscard]] StepOutcome dispatch(const SequenceNode& node, const StepContext& context) const;

private:
    std::map<std::string, StepKindDescriptor, std::less<>> descriptors_;
    std::map<std::string, StepHandler, std::less<>>        handlers_;
};

/** @brief Return the stable lowercase spelling of a step shape. */
[[nodiscard]] const char* stepShapeName(StepShape shape) noexcept;

}  // namespace eve::dnut
