#pragma once
/** @file AttributeSet.h
 * @brief attributes module-interface: canonical attribute facts and unpublished restore preparation.
 * @remarks Live sets expose IStateQuery/IStateMutation; prepared copies register no capabilities.
 * ECS owners retain their sets; preparation creates no entities or Links. No events, script bindings or tool
 * endpoints are added by staging. Prepared state is unpublished; the attributes.set v1 value codec preserves canonical
 * facts. Domain owners still join projection metadata, policy implementations and world lifetimes.
 */
#include "common/Export.h"


#include "common/Result.h"
#include "common/Value.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eve::attributes {

using AttributeId      = std::string;
using ModifierId       = std::string;
using SourceId         = std::string;
using ModifierPriority = std::int32_t;
using ModifierSequence = std::uint64_t;

/** @brief Built-in calculation operation for an attribute modifier. */
enum class AttributeOperation : std::uint8_t {
    Add,
    AdditivePercent,
    MultiplicativePercent,
    Override,
    ClampMin,
    ClampMax,
    Custom,
};

/**
 * @brief Stable spelling of a built-in attribute operation.
 * @ownership The returned string is a borrowed, process-static literal; the
 * caller must not free or retain it as mutable storage.
 * @nullable Never null for a valid enum value; the unknown-value spelling is also
 * a process-static literal for an invalid value.
 * @lifetime Valid for the process lifetime.
 * @thread Thread-safe because the function reads no mutable state.
 * @reentrancy Reentrant; it performs no callbacks.
 */
[[nodiscard]] const char* attributeOperationName(AttributeOperation operation) noexcept;

/**
 * @brief Parsed legacy operation and its normalized value.
 *
 * `multiply` is accepted only by compatibility facades and is normalized from
 * a factor (`1.5`) to a percentage delta (`0.5`). The canonical API always
 * stores percentage deltas (`0.1` means +10%).
 */
struct ParsedAttributeOperation {
    AttributeOperation operation = AttributeOperation::Add;
    double             value     = 0.0;
};

/**
 * @brief Parse a built-in operation spelling used by existing data and APIs.
 * @return A normalized operation, or a rejected Result for an unknown spelling.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION eve::Result<ParsedAttributeOperation> parseAttributeOperation(
    std::string_view operation, double value);

/** @brief Registry owning explicitly named custom attribute policies and their optional implementation revision.
 * @thread Owner thread only; callbacks must not mutate this registry during apply. No locks are held.
 * @remarks Revisions are publisher assertions about the actual callback, not hashes inferred from std::function.
 * Persistent callers must compare the expected revision and reject absent/unversioned implementations.
 */
class EVENGINE_API_FOUNDATION AttributeOperationRegistry {
public:
    using Function = std::function<double(double current, double value)>;

    /** @brief Register or replace a named policy; calls execute on the caller thread. */
    void registerOperation(const std::string& policyId, Function function);
    /** @brief Atomically install an owning callback together with its publisher-declared immutable revision.
     * @param policyId Logical operation name; must be nonempty.
     * @param revision Stable implementation revision (for example a build/source digest), not a runtime generation.
     * @param function Owned executable implementation; must be nonempty. Never invoked during registration.
     * @return Success, or InvalidArgument without replacing the previous callback/revision.
     * @remarks The publisher must change revision when behavior changes. Legacy registration deliberately removes
     * revision identity. Independent registry copies own independent records. Querying does not execute callbacks.
     * @throws std::bad_alloc Allocation failed; the previous record remains intact.
     */
    [[nodiscard]] Result<void> registerVersionedOperation(const std::string& policyId, std::string revision,
                                                          Function function);
    /** @brief Copy the actual installed callback's declared revision without executing it.
     * @return Owning revision, NotFound for absent operations, or Unsupported for legacy/unversioned registrations.
     * @ownership Returned text is independent of registry replacement/destruction.
     * @throws std::bad_alloc Output allocation failed; registry is unchanged.
     */
    [[nodiscard]] Result<std::string> implementationRevision(const std::string& policyId) const;
    /** @brief Remove a named policy and its revision. */
    void unregisterOperation(const std::string& policyId);
    /** @brief Return whether a policy is registered. */
    bool has(const std::string& policyId) const;
    /** @brief Apply a policy, returning current unchanged when it is absent. */
    double apply(const std::string& policyId, double current, double value) const;

    /** @brief Compatibility spelling for RPG's former custom-op facade. */
    void registerOp(const std::string& policyId, Function function);
    /** @brief Compatibility spelling for RPG's former custom-op facade. */
    void unregisterOp(const std::string& policyId);

private:
    struct Operation {
        Function    function;
        std::string revision;
    };
    void                                       install(const std::string& policyId, Operation operation);
    std::unordered_map<std::string, Operation> operations_;
};

/**
 * @brief One canonical runtime modifier applied to one named attribute.
 *
 * `id`, `attribute`, and `source` are stable domain strings. `sequence` is
 * assigned by AttributeSet and breaks equal-priority ties. For Custom,
 * `policyId` names a registered AttributeOperationRegistry policy.
 */
struct EVENGINE_API_FOUNDATION AttributeModifier {
    ModifierId         id;
    AttributeId        attribute;
    SourceId           source;
    AttributeOperation operation = AttributeOperation::Add;
    double             value     = 0.0;
    ModifierPriority   priority  = 0;
    ModifierSequence   sequence  = 0;
    std::string        policyId;

    /** @brief Attribute modifier. */
    AttributeModifier() = default;

    /** @brief Construct a canonical modifier with an explicit operation. */
    AttributeModifier(ModifierId modifierId, AttributeId attributeId, SourceId sourceId,
                      AttributeOperation operationValue, double modifierValue, ModifierPriority priorityValue = 0,
                      ModifierSequence sequenceValue = 0, std::string policy = {})
        /** @brief Id. */
        : id(std::move(modifierId)),
          /** @brief Attribute. */
          attribute(std::move(attributeId)),
          /** @brief Source. */
          source(std::move(sourceId)),
          /** @brief Operation. */
          operation(operationValue),
          /** @brief Value. */
          value(modifierValue),
          /** @brief Priority. */
          priority(priorityValue),
          /** @brief Sequence. */
          sequence(sequenceValue),
          /** @brief Policy id. */
          policyId(std::move(policy)) {}

    /**
     * @brief Construct the former per-attribute RPG test shape.
     * @remarks The attribute is intentionally empty and must be supplied before
     *          inserting into an AttributeSet.
     */
    AttributeModifier(ModifierId modifierId, SourceId sourceId, std::string_view operationName, double modifierValue,
                      ModifierPriority priorityValue = 0);
};

/** @brief Runtime value and modifiers for exactly one attribute. */
struct AttributeValue {
    double                         base = 0.0;
    std::vector<AttributeModifier> modifiers;
    mutable double                 cached = 0.0;
    mutable bool                   dirty  = true;
};

/**
 * @brief Compute one attribute using the canonical operation order.
 *
 * The order is base, all Add values, one summed AdditivePercent pass, all
 * MultiplicativePercent factors, then priority/sequence ordered Override and
 * Clamp operations. Custom policies participate in the final ordered phase.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION double computeAttributeValue(
    const AttributeValue& attribute, const AttributeOperationRegistry* customOperations = nullptr);

class AttributeSet;
/** @brief Owning unpublished attribute candidate; destruction cancels locally without capability registration.
 * @ownership Owns a detached canonical AttributeSet; no source/target pointers are retained.
 * @thread Owner-thread use; no callbacks during staging or commit.
 */
class EVENGINE_API_FOUNDATION PreparedAttributeSet {
public:
    /** @brief Release unpublished data. */
    ~PreparedAttributeSet();
    /** @brief Transfer unpublished data. */
    PreparedAttributeSet(PreparedAttributeSet&&) noexcept;
    /** @brief Cancel this candidate and transfer another. */
    PreparedAttributeSet& operator=(PreparedAttributeSet&&) noexcept;
    PreparedAttributeSet(const PreparedAttributeSet&)            = delete;
    PreparedAttributeSet& operator=(const PreparedAttributeSet&) = delete;
    /** @brief Borrow the canonical candidate until move, destruction or commit; no implicit publication. */
    [[nodiscard]] const AttributeSet& view() const noexcept;
    /** @brief Edit a base value in unpublished data using the canonical setter.
     * @cost Expected constant lookup plus allocation for a new attribute, per edited field.
     */
    void setBase(const AttributeId& attribute, double value);

private:
    friend class AttributeSet;
    explicit PreparedAttributeSet(std::unique_ptr<AttributeSet> candidate) noexcept;
    std::unique_ptr<AttributeSet> candidate_;
};

class AttributeSetStateAdapter;

/** @brief Generic owning collection of canonical attributes and modifiers. */
class EVENGINE_API_FOUNDATION AttributeSet {
public:
    /** @brief Construct a set associated with an optional stable subject id. */
    explicit AttributeSet(std::string subject = {});
    /** @brief Withdraw world-state capability listeners. */
    ~AttributeSet();
    /** @brief Copy a set while rebuilding its internal borrowed enumeration view. */
    AttributeSet(const AttributeSet& other);
    /** @brief Copy a set while rebuilding its internal borrowed enumeration view. */
    AttributeSet& operator=(const AttributeSet& other);
    /** @brief Move a set while rebuilding its internal borrowed enumeration view. */
    AttributeSet(AttributeSet&& other) noexcept;
    /** @brief Move-assign a set while rebuilding its internal borrowed enumeration view. */
    AttributeSet& operator=(AttributeSet&& other) noexcept;

    /** @brief Prepare empty unpublished canonical values for a new domain object.
     * @return Owning candidate with no subject, attributes, modifiers or capability registrations.
     * @thread Owner thread; no callbacks or locks.
     * @cost One owning candidate allocation, once per new object preparation.
     */
    [[nodiscard]] static PreparedAttributeSet prepareEmpty();
    /** @brief Copy canonical values into an opaque candidate without registering world-state capabilities.
     * @return Owning unpublished candidate; source remains unchanged and need not outlive the result.
     * @cost Linear in attributes and modifiers with owning allocation, once per restore preparation.
     * @thread Owner thread; source must remain stable for this call. No callbacks or locks.
     */
    [[nodiscard]] PreparedAttributeSet prepareCopy() const;
    /** @brief Encode attributes.set v1 canonical values, exact modifiers and allocation watermarks.
     * @return Owning versioned value or invalid/nonfinite state diagnostics; source caches and adapters unchanged.
     * @remarks Derived cache/order pointers are excluded. Custom policy ids are declarations, not execution
     * authorization; domain restoration must admit matching external implementation revisions separately.
     * @thread Stable source on owner thread; no policies, callbacks or capability operations.
     * @cost Sorts attribute ids and copies value/modifier-sized facts per explicit persistent encoding.
     */
    [[nodiscard]] Result<Value> encodeState() const;
    /** @brief Admit strict attributes.set v1 into detached canonical storage without renumbering modifiers.
     * @param state Borrowed versioned value only during this call; all accepted facts are copied.
     * @return Unpublished candidate or version/field/identity/range/counter diagnostics; no live set is changed.
     * @remarks Unknown fields/versions reject, with no implicit migration. Derived caches start dirty and are
     * computed only on later explicit value queries; no custom policies run. Publication owner must check subject,
     * physical lifetime and external implementation bindings before commitPrepared.
     * @thread Owner thread, outside callbacks; no adapter registration.
     * @cost Copies and indexes all attribute/modifier facts once per explicit admission.
     */
    [[nodiscard]] static Result<PreparedAttributeSet> prepareState(const Value& state);

    /** @brief Consume a non-moved candidate and replace values without callbacks or allocation.
     * @remarks Retains this set's subject and existing capability adapter; does not publish a detached target.
     * Candidate validation and target lifetime/phase checks belong to the owning restore transaction.
     * @thread Owner thread outside iteration/callbacks; no reentry. Candidate must not have been consumed.
     */
    void commitPrepared(PreparedAttributeSet&& candidate) noexcept;

    /** @brief Stable game-defined id of the entity or object owning this set. */
    const std::string& subject() const;
    /** @brief Set or replace a base value. Empty attribute ids are ignored for compatibility. */
    void setBase(const AttributeId& attribute, double value);
    /** @brief Add delta to a base value, creating it from zero when absent. */
    void modifyBase(const AttributeId& attribute, double delta);
    /** @brief Return whether a base value or modifier names the attribute. */
    bool has(const AttributeId& attribute) const;
    /** @brief Return a base value, or fallback when absent. */
    double getBase(const AttributeId& attribute, double fallback = 0.0) const;
    /**
     * @brief Return the deterministic final value after all modifiers.
     * @param customOperations Optional borrowed policy registry. When supplied,
     *        custom policies are evaluated and the value is not cached across
     *        registry changes.
     */
    double getFinal(const AttributeId& attribute, double fallback = 0.0,
                    const AttributeOperationRegistry* customOperations = nullptr) const;

    /**
     * @brief Add or replace a canonical modifier and return its stable id.
     * @return Rejected for invalid attribute/custom policy data; the returned
     *         id is owned by the Result and remains valid for this set.
     */
    [[nodiscard]] eve::Result<ModifierId> addModifier(AttributeModifier modifier);

    /**
     * @brief Compatibility facade for the former string operation API.
     * @return false only for invalid legacy input; canonical computation still
     *         happens in addModifier(AttributeModifier).
     */
    bool addModifier(const std::string& id, const std::string& attribute, const std::string& source,
                     const std::string& operation, double value, ModifierPriority priority = 0);

    /** @brief Remove one modifier with a structured result. */
    [[nodiscard]] eve::Result<void> removeModifier(const ModifierId& id);
    /** @brief Remove one modifier from a named attribute with a structured result. */
    [[nodiscard]] eve::Result<void> removeModifier(const AttributeId& attribute, const ModifierId& id);
    /** @brief Remove source modifiers with a structured result containing the count. */
    [[nodiscard]] eve::Result<int> removeBySource(const SourceId& source, const AttributeId& attribute = {});
    /** @brief Mark one attribute dirty after an external policy or restore change. */
    void invalidate(const AttributeId& attribute);
    /** @brief Remove all modifiers. */
    void clearModifiers();
    /** @brief Number of modifiers. */
    int modifierCount() const;
    /** @brief Return a modifier by deterministic sequence order, or null when out of range. */
    const AttributeModifier* modifierAt(int index) const;
    /** @brief Number of named attribute values, including modifier-only values. */
    int attributeCount() const;
    /** @brief Return an attribute id in deterministic lexical order, or an empty string. */
    std::string attributeAt(int index) const;

private:
    struct DetachedCopyTag {};
    explicit AttributeSet(DetachedCopyTag);
    AttributeSet(const AttributeSet& other, DetachedCopyTag);
    void rebuildOrder() const;
    static bool isValidOperation(const AttributeModifier& modifier) noexcept;
    void        publishWorldAdapter();
    void        withdrawWorldAdapter();

    std::string                                     subject_;
    std::unordered_map<AttributeId, AttributeValue> values_;
    mutable std::vector<const AttributeModifier*>   order_;
    mutable bool                                    orderDirty_     = true;
    ModifierSequence                                nextSequence_   = 1;
    ModifierSequence                                nextModifierId_ = 1;
    std::unique_ptr<AttributeSetStateAdapter>       worldAdapter_;
};

}  // namespace eve::attributes
