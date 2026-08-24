#pragma once

#include "editor/EditorHostProfile.h"
#include "editor/EditorProperty.h"

#include <vector>

namespace eve::editor {

/** @brief Read-only row produced for a concrete property UI adapter. */
struct PropertyPresentationRow {
    PropertyDescriptor descriptor;
    PropertyReadResult value;
};

/** @brief Immutable presentation model consumed by developer or game UI. */
struct PropertyPresentation {
    std::string                          typeId;
    std::uint32_t                        schemaVersion = 1;
    std::vector<PropertyPresentationRow> rows;
};

/** @brief Command intent emitted by a presenter instead of calling a setter. */
struct PropertyEditIntent {
    CommandId   command = CommandId("editor.property.set");
    EditorValue payload;
};

/** @brief Shared presenter logic that exposes developer-oriented property rows. */
class DeveloperPropertyPresenter {
public:
    /** @brief Read every schema property, including advanced/editor-only rows. */
    PropertyPresentation present(const PropertySchema& schema, const SelectionSnapshot& selection,
                                 const IPropertyProvider& provider) const;
    /** @brief Validate a candidate and emit a command payload without mutating the provider. */
    EditorResult<PropertyEditIntent> editIntent(const PropertySchema& schema, const SelectionSnapshot& selection,
                                                const PropertyPath& path, const EditorValue& value,
                                                PropertySetMode mode = PropertySetMode::Absolute) const;
};

/** @brief Shared presenter logic that exposes only runtime-safe property rows. */
class RuntimePropertyPresenter {
public:
    /** @brief Read properties marked Runtime and not marked EditorOnly. */
    PropertyPresentation present(const PropertySchema& schema, const SelectionSnapshot& selection,
                                 const IPropertyProvider& provider, const HostProfile& profile) const;
    /** @brief Validate a runtime-visible candidate and emit the same command payload as developer UI. */
    EditorResult<PropertyEditIntent> editIntent(const PropertySchema& schema, const SelectionSnapshot& selection,
                                                const PropertyPath& path, const EditorValue& value,
                                                const HostProfile& profile,
                                                PropertySetMode    mode = PropertySetMode::Absolute) const;
};

}  // namespace eve::editor
