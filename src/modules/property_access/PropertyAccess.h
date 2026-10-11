#pragma once

#include "common/BorrowedRef.h"
#include "common/Export.h"
#include "common/Result.h"
#include "common/Subscription.h"
#include "common/Value.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eve::property_access {

/** @brief Semantic property kind used by renderer-independent presenters. */
enum class PropertyKind {
    Auto,
    Bool,
    Integer,
    Number,
    String,
    Enum,
    Color,
    Vec2,
    Vec3,
    Vec4,
    AssetRef,
    ObjectRef,
    Struct,
    Array,
    Map,
    Action,
    ReadOnlyText
};

/** @brief Cross-host property visibility and editing flags. */
enum class PropertyFlag : std::uint64_t {
    None       = 0,
    ReadOnly   = 1ull << 0,
    Advanced   = 1ull << 1,
    EditorOnly = 1ull << 2,
    Runtime    = 1ull << 3,
    Transient  = 1ull << 4,
    Dangerous  = 1ull << 5,
    MultiEdit  = 1ull << 6
};

/** @brief Operator |. */
constexpr PropertyFlag operator|(PropertyFlag left, PropertyFlag right) {
    return static_cast<PropertyFlag>(static_cast<std::uint64_t>(left) | static_cast<std::uint64_t>(right));
}
/** @brief Operator &. */
constexpr PropertyFlag operator&(PropertyFlag left, PropertyFlag right) {
    return static_cast<PropertyFlag>(static_cast<std::uint64_t>(left) & static_cast<std::uint64_t>(right));
}
/** @brief True when flag. */
constexpr bool hasFlag(PropertyFlag value, PropertyFlag flag) { return (value & flag) == flag; }

/** @brief Numeric editing metadata independent of a slider or spin-box widget. */
struct NumericMetadata {
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::optional<double> step;
    std::string           units;
    int                   precision = 3;
};

/** @brief One stable property description consumed by any UI shell. */
struct PropertyDescriptor {
    std::string              path;
    std::string              displayName;
    std::string              description;
    std::string              category;
    PropertyKind             kind  = PropertyKind::Auto;
    PropertyFlag             flags = PropertyFlag::None;
    Value                    defaultValue;
    NumericMetadata          numeric;
    std::vector<std::string> choices;
    std::string              presenterHint;
};

/** @brief Versioned schema for one view model or editable target. */
struct EVENGINE_API_FOUNDATION PropertySchema {
    std::string                     typeId;
    std::uint32_t                   version = 1;
    std::vector<PropertyDescriptor> properties;

    /** @brief Find a property by stable path, or an empty immediate borrow. */
    [[nodiscard]] eve::OptionalRef<const PropertyDescriptor> find(const std::string &path) const;
};

/** @brief Reserved DiagnosticDetails key carrying a stable property-access rule id. */
inline constexpr const char *kRuleDiagnosticDetail = "rule";

/**
 * @brief Build a rejected write/validation result with a stable rule string.
 * @param rule Stable machine-readable rule id (for example `property_access.property.type`).
 * @param message Human-readable explanation.
 * @param code Coarse diagnostic category; defaults to precondition violation.
 * @return Failed `Result<void>` with `StatusCode::Rejected`.
 */
[[nodiscard]] inline Result<void> rejected(std::string rule, std::string message,
                                           DiagnosticCode code = DiagnosticCode::PreconditionViolation) {
    DiagnosticDetails details;
    details.emplace_back(kRuleDiagnosticDetail, std::move(rule));
    return Result<void>::failure(Status::failure(
        StatusCode::Rejected, Diagnostic::error(code, std::move(message), {}, std::move(details), "property_access")));
}

/**
 * @brief Build a successful write/validation result.
 * @return Successful `Result<void>` with `StatusCode::Ok`.
 */
[[nodiscard]] inline Result<void> accepted() { return Result<void>::success(); }

/**
 * @brief Return the projected property-access rule id, or empty when absent.
 * @param diagnostic Structured diagnostic that may carry a `rule` detail.
 */
[[nodiscard]] inline std::string_view diagnosticRule(const Diagnostic &diagnostic) {
    for (const auto &[key, value] : diagnostic.details())
        if (key == kRuleDiagnosticDetail) return value;
    return {};
}

/**
 * @brief Return the primary rule id from a write/validation result.
 * @param result Observed property-access Result; success yields an empty string.
 */
[[nodiscard]] inline std::string writeRule(const Result<void> &result) {
    if (const Diagnostic *error = result.error()) return std::string(diagnosticRule(*error));
    return {};
}

/**
 * @brief Validate one candidate value against the shared property contract.
 * @param property Property kind, flags, choices and numeric constraints.
 * @param value Candidate value. Floating-point values must be finite.
 * @return Successful Result on accept; Rejected Result with a `rule` detail otherwise.
 *
 * This is the single semantic validation entry point for property-access property
 * adapters. Host-specific adapters may translate its diagnostics, but must not
 * reimplement kind, enum, finite, arity or numeric-range validation.
 *
 * Color requires an Array of 4 numeric components; Vec2/Vec3/Vec4 require 2/3/4.
 * Rejected composites use `property_access.property.arity` when the length is wrong.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<void> validatePropertyValue(const PropertyDescriptor &property,
                                                                         const Value              &value);

/** @brief Availability of a property in an immutable model snapshot. */
enum class PropertyChangeState { Value, Mixed, Missing };

/** @brief Immutable notification emitted after a model property changes. */
struct PropertyChange {
    std::string   path;
    Value         value;
    std::uint64_t revision = 0;
    PropertyChangeState state = PropertyChangeState::Value;
};

/**
 * @brief Compatibility alias for the common observer lifetime token.
 *
 * The common `eve::Subscription` owns the only cancellation lifecycle. This
 * alias preserves the property-access API without maintaining a second token
 * implementation.
 */
using Subscription = eve::Subscription;

/**
 * @brief Renderer- and transaction-independent MVVM property surface.
 *
 * Implementations may wrap gameplay state, reflected script objects, editor
 * targets or remote automation. Writes express intent; the implementation
 * decides whether to assign directly, dispatch a command, or reject it.
 */
class EVENGINE_API_FOUNDATION_INLINE IPropertyAccess {
public:
    using ChangeCallback = std::function<void(const PropertyChange &)>;

    /** @brief I property access. */
    virtual ~IPropertyAccess() = default;
    /** @brief Return the stable schema exposed by this model. */
    virtual const PropertySchema &schema() const = 0;
    /** @brief Read a property value, or nullopt when it is absent. */
    virtual std::optional<Value> read(const std::string &path) const = 0;
    /**
     * @brief Request a two-way binding write.
     * @return Successful Result when the write is accepted; Rejected/Failed with a
     *         `rule` diagnostic detail when validation or the authority refuses it.
     */
    [[nodiscard]] virtual Result<void> write(const std::string &path, const Value &value) = 0;
    /** @brief Monotonic revision incremented after observable changes. */
    virtual std::uint64_t revision() const = 0;
    /** @brief Observe changes until the returned token is destroyed. */
    virtual Subscription subscribe(ChangeCallback callback) = 0;
};

}  // namespace eve::property_access
