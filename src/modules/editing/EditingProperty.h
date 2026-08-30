#pragma once

#include "editing/EditingProtocol.h"
#include "editing/EditingSelection.h"
#include "common/Result.h"
#include "common/Revision.h"
#include "property_access/PropertyAccess.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace eve::editing {

enum class PropertyType {
    Bool,
    Int,
    Float,
    String,
    Enum,
    Color,
    Vec2,
    Vec3,
    Vec4,
    Transform,
    AssetRef,
    ObjectRef,
    Struct,
    Array,
    Map,
    Action,
    ReadOnlyText
};

enum class PropertyFlag : std::uint64_t {
    None       = 0,
    ReadOnly   = 1ull << 0,
    Advanced   = 1ull << 1,
    EditorOnly = 1ull << 2,
    Runtime    = 1ull << 3,
    Replicated = 1ull << 4,
    Transient  = 1ull << 5,
    Dangerous  = 1ull << 6,
    MultiEdit  = 1ull << 7
};

constexpr PropertyFlag operator|(PropertyFlag left, PropertyFlag right) {
    return static_cast<PropertyFlag>(static_cast<std::uint64_t>(left) | static_cast<std::uint64_t>(right));
}
constexpr PropertyFlag operator&(PropertyFlag left, PropertyFlag right) {
    return static_cast<PropertyFlag>(static_cast<std::uint64_t>(left) & static_cast<std::uint64_t>(right));
}
constexpr bool hasPropertyFlag(PropertyFlag value, PropertyFlag flag) { return (value & flag) == flag; }

struct NumericMetadata {
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::optional<double> step;
    std::string           units;
    int                   precision = 3;
};

struct PropertyDescriptor {
    PropertyPath             path;
    std::string              displayNameKey;
    std::string              descriptionKey;
    std::string              category;
    PropertyType             type  = PropertyType::String;
    PropertyFlag             flags = PropertyFlag::None;
    Value                    defaultValue;
    NumericMetadata          numeric;
    std::vector<std::string> enumItems;
    std::vector<std::string> assetTypeFilters;
    std::string              presenterHint;
    std::vector<RuleId>      validators;
};

class PropertyDescriptorLookup {
public:
    PropertyDescriptorLookup() = default;
    explicit PropertyDescriptorLookup(PropertyDescriptor descriptor) : descriptor_(std::move(descriptor)) {}
    [[nodiscard]] explicit                  operator bool() const noexcept { return descriptor_.has_value(); }
    [[nodiscard]] const PropertyDescriptor& operator*() const { return descriptor_.value(); }
    [[nodiscard]] const PropertyDescriptor* operator->() const { return &descriptor_.value(); }
    friend bool operator==(const PropertyDescriptorLookup& lookup, std::nullptr_t) noexcept {
        return !lookup.descriptor_;
    }
    friend bool operator!=(const PropertyDescriptorLookup& lookup, std::nullptr_t) noexcept {
        return lookup.descriptor_.has_value();
    }

private:
    std::optional<PropertyDescriptor> descriptor_;
};

struct PropertySchema {
    std::string                            typeId;
    std::uint32_t                          version = 1;
    std::vector<PropertyDescriptor>        properties;
    [[nodiscard]] PropertyDescriptorLookup find(const PropertyPath& path) const;
};

enum class PropertyReadState { Value, Mixed, Missing, Error };

struct PropertyReadResult {
    PropertyReadState       state = PropertyReadState::Missing;
    Value                   value;
    std::vector<Diagnostic> diagnostics;
};

enum class PropertySetMode { Absolute, Relative, Reset };

/** @brief UI-independent property access for an immutable selection snapshot. */
class IPropertyProvider {
public:
    virtual ~IPropertyProvider() = default;
    /** @brief Stable capability identity for generic property editing. */
    static CapabilityId editingCapabilityId() { return CapabilityId("eve.editor.target.material-properties"); }
    [[nodiscard]] virtual eve::Result<eve::Revision> currentRevision(const SelectionSnapshot& selection) const {
        (void)selection;
        return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported,
            "Property provider must implement currentRevision; implicit revision zero is not allowed",
            "authoring.property.current-revision", {}, "authoring.IPropertyProvider"));
    }
    virtual PropertySchema     schema(const SelectionSnapshot& selection) const                           = 0;
    virtual PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const   = 0;
    [[nodiscard]] virtual Result<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                                          const Value& value, PropertySetMode mode) const = 0;
    [[nodiscard]] virtual Result<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                                            const PropertyPath&      path) const          = 0;
};

[[nodiscard]] Result<void> validatePropertyValue(const PropertyDescriptor& descriptor, const Value& value);
[[nodiscard]] property_access::PropertyDescriptor toPresentationDescriptor(const PropertyDescriptor& source);
[[nodiscard]] eve::Value                          toPresentationValue(const Value& value);
[[nodiscard]] Value                               toEditingValue(const eve::Value& value);

}  // namespace eve::editing
