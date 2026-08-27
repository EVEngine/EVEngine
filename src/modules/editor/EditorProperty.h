#pragma once

#include "common/Result.h"
#include "common/Revision.h"
#include "editor/EditorProtocol.h"
#include "editor/EditorSelection.h"
#include "property_access/PropertyAccess.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief Semantic value kind used to select property presenters and validators. */
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

/** @brief Discovery, editing and build exposure flags for a property. */
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

/** @brief Numeric range, stepping and display metadata. */
struct NumericMetadata {
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::optional<double> step;
    std::string           units;
    int                   precision = 3;
};

/** @brief One stable property definition independent of UI widgets. */
struct PropertyDescriptor {
    PropertyPath             path;
    std::string              displayNameKey;
    std::string              descriptionKey;
    std::string              category;
    PropertyType             type  = PropertyType::String;
    PropertyFlag             flags = PropertyFlag::None;
    EditorValue              defaultValue;
    NumericMetadata          numeric;
    std::vector<std::string> enumItems;
    std::vector<std::string> assetTypeFilters;
    std::string              presenterHint;
    std::vector<RuleId>      validators;
};

/** @brief Versioned property schema shared by developer, runtime and automation hosts. */
struct PropertySchema {
    std::string                     typeId;
    std::uint32_t                   version = 1;
    std::vector<PropertyDescriptor> properties;

    /** @brief Find one property by stable path, or nullptr. */
    const PropertyDescriptor* find(const PropertyPath& path) const;
};

/** @brief Read state for single- and multi-selection property queries. */
enum class PropertyReadState { Value, Mixed, Missing, Error };

/** @brief Property value snapshot and any read diagnostics. */
struct PropertyReadResult {
    PropertyReadState             state = PropertyReadState::Missing;
    EditorValue                   value;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Meaning of a property assignment request. */
enum class PropertySetMode { Absolute, Relative, Reset };

/** @brief UI-independent access to properties of an immutable selection snapshot. */
class IPropertyProvider {
public:
    virtual ~IPropertyProvider() = default;

    /**
     * @brief Return the provider's authoritative revision for this selection.
     * @param selection Immutable selection whose properties are being edited.
     * @return The current strong revision, or `Unsupported` when a legacy
     *         provider has not implemented the revision contract.
     * @remarks The compatibility default fails closed. It never reports zero,
     *          because zero would hide an external change and permit a stale
     *          editor model to overwrite newer state. Providers that can be
     *          edited must override this method and return their authoritative
     *          revision, including a non-zero initial revision.
     */
    [[nodiscard]] virtual eve::Result<eve::Revision> currentRevision(
        const SelectionSnapshot& selection) const {
        (void)selection;
        return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported,
            "Property provider must implement currentRevision; implicit revision zero is not allowed",
            "editor.property.current-revision", {}, "editor.IPropertyProvider"));
    }

    /** @brief Return the compatible schema for the captured selection. */
    virtual PropertySchema schema(const SelectionSnapshot& selection) const = 0;
    /** @brief Read one property without returning internal references. */
    virtual PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const = 0;
    /** @brief Build, but do not apply, a property mutation operation. */
    virtual EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                                  const EditorValue& value, PropertySetMode mode) const = 0;
    /** @brief Build, but do not apply, a reset-to-default operation. */
    virtual EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                                    const PropertyPath&      path) const = 0;
};

/**
 * @brief Validate a value against one descriptor's shared and editor rules.
 * @param descriptor Property contract.
 * @param value Candidate structured value.
 * @return Applied when valid, otherwise a structured rejection. Type, enum,
 * finite and numeric-range checks are delegated to presentation's shared
 * validator; editor-only rules remain in this adapter.
 */
EditorResult<void> validatePropertyValue(const PropertyDescriptor& descriptor, const EditorValue& value);

/**
 * @brief Convert an editor descriptor to the shared property-access contract.
 * @param source Editor descriptor, including editor-only extensions.
 * @return Presentation descriptor containing only renderer-independent fields.
 */
property_access::PropertyDescriptor toPresentationDescriptor(const PropertyDescriptor& source);

/**
 * @brief Convert an editor value tree to the shared presentation value tree.
 * @param value Editor-owned deterministic value tree.
 * @return Equivalent renderer-independent value tree.
 */
eve::Value toPresentationValue(const EditorValue &value);
/**
 * @brief Convert a shared presentation value tree to the editor value tree.
 * @param value Renderer-independent deterministic value tree.
 * @return Equivalent editor value tree.
 */
EditorValue toEditorValue(const eve::Value &value);

}  // namespace eve::editor
