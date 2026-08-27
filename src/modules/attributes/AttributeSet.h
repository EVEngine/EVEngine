#pragma once

#include "common/Result.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eve::attributes {

using AttributeId = std::string;
using ModifierId = std::string;
using SourceId = std::string;
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
 * @nullable Never null for a valid enum value; the fallback spelling is also
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
    double value = 0.0;
};

/**
 * @brief Parse a built-in operation spelling used by existing data and APIs.
 * @return A normalized operation, or a rejected Result for an unknown spelling.
 */
[[nodiscard]] eve::Result<ParsedAttributeOperation> parseAttributeOperation(std::string_view operation,
                                                                              double value);

/** @brief Registry for explicitly named custom attribute policies. */
class AttributeOperationRegistry {
public:
    using Function = std::function<double(double current, double value)>;

    /** @brief Register or replace a named policy; calls execute on the caller thread. */
    void registerOperation(const std::string& policyId, Function function);
    /** @brief Remove a named policy. */
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
    std::unordered_map<std::string, Function> operations_;
};

/**
 * @brief One canonical runtime modifier applied to one named attribute.
 *
 * `id`, `attribute`, and `source` are stable domain strings. `sequence` is
 * assigned by AttributeSet and breaks equal-priority ties. For Custom,
 * `policyId` names a registered AttributeOperationRegistry policy.
 */
struct AttributeModifier {
    ModifierId id;
    AttributeId attribute;
    SourceId source;
    AttributeOperation operation = AttributeOperation::Add;
    double value = 0.0;
    ModifierPriority priority = 0;
    ModifierSequence sequence = 0;
    std::string policyId;

    AttributeModifier() = default;

    /** @brief Construct a canonical modifier with an explicit operation. */
    AttributeModifier(ModifierId modifierId, AttributeId attributeId, SourceId sourceId,
                      AttributeOperation operationValue, double modifierValue,
                      ModifierPriority priorityValue = 0,
                      ModifierSequence sequenceValue = 0,
                      std::string policy = {})
        : id(std::move(modifierId)), attribute(std::move(attributeId)), source(std::move(sourceId)),
          operation(operationValue), value(modifierValue), priority(priorityValue),
          sequence(sequenceValue), policyId(std::move(policy)) {}

    /**
     * @brief Construct the former per-attribute RPG test shape.
     * @remarks The attribute is intentionally empty and must be supplied before
     *          inserting into an AttributeSet.
     */
    AttributeModifier(ModifierId modifierId, SourceId sourceId, std::string_view operationName,
                      double modifierValue, ModifierPriority priorityValue = 0);
};

/** @brief Runtime value and modifiers for exactly one attribute. */
struct AttributeValue {
    double base = 0.0;
    std::vector<AttributeModifier> modifiers;
    mutable double cached = 0.0;
    mutable bool dirty = true;
};

/**
 * @brief Compute one attribute using the canonical operation order.
 *
 * The order is base, all Add values, one summed AdditivePercent pass, all
 * MultiplicativePercent factors, then priority/sequence ordered Override and
 * Clamp operations. Custom policies participate in the final ordered phase.
 */
[[nodiscard]] double computeAttributeValue(const AttributeValue& attribute,
                                            const AttributeOperationRegistry* customOperations = nullptr);

/** @brief Generic owning collection of canonical attributes and modifiers. */
class AttributeSet {
public:
    /** @brief Construct a set associated with an optional stable subject id. */
    explicit AttributeSet(std::string subject = {});
    /** @brief Copy a set while rebuilding its internal borrowed enumeration view. */
    AttributeSet(const AttributeSet& other);
    /** @brief Copy a set while rebuilding its internal borrowed enumeration view. */
    AttributeSet& operator=(const AttributeSet& other);
    /** @brief Move a set while rebuilding its internal borrowed enumeration view. */
    AttributeSet(AttributeSet&& other) noexcept;
    /** @brief Move-assign a set while rebuilding its internal borrowed enumeration view. */
    AttributeSet& operator=(AttributeSet&& other) noexcept;

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
    [[nodiscard]] eve::Result<void> removeModifier(const AttributeId& attribute,
                                                   const ModifierId& id);
    /** @brief Remove source modifiers with a structured result containing the count. */
    [[nodiscard]] eve::Result<int> removeBySource(const SourceId& source,
                                                  const AttributeId& attribute = {});
    /** @brief Mark one attribute dirty after an external policy or restore change. */
    void invalidate(const AttributeId& attribute);
    /** @brief Remove all modifiers. */
    void clearModifiers();
    /** @brief Number of modifiers. */
    int modifierCount() const;
    /** @brief Return a modifier by deterministic sequence order, or null when out of range. */
    const AttributeModifier* modifierAt(int index) const;

private:
    void rebuildOrder() const;
    static bool isValidOperation(const AttributeModifier& modifier) noexcept;

    std::string subject_;
    std::unordered_map<AttributeId, AttributeValue> values_;
    mutable std::vector<const AttributeModifier*> order_;
    mutable bool orderDirty_ = true;
    ModifierSequence nextSequence_ = 1;
    ModifierSequence nextModifierId_ = 1;
};

}  // namespace eve::attributes
