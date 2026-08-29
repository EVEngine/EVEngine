#pragma once

#include "editor/EditorDefinitionTarget.h"
#include "editor/EditorProperty.h"

namespace eve::schema {
struct SchemaDefinition;
}

namespace eve::editor {

/** @brief Schema-driven property adapter for one definition document. */
class DefinitionSchemaFormTarget final : public IPropertyProvider {
public:
    /** @brief Bind a document and exact immutable schema; both must outlive this adapter. */
    DefinitionSchemaFormTarget(DefinitionDocument* document,
                               const schema::SchemaDefinition* schema);
    eve::Result<eve::Revision> currentRevision(const SelectionSnapshot& selection) const override;
    PropertySchema schema(const SelectionSnapshot& selection) const override;
    PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    EditorResult<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    EditorResult<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath& path) const override;
    /** @brief Diagnose schema/document identity, missing required fields and current value constraints. */
    std::vector<EditorDiagnostic> validate() const;

private:
    bool matches(const SelectionSnapshot& selection) const;
    DefinitionDocument* document_ = nullptr;
    const schema::SchemaDefinition* schema_ = nullptr;
};

}  // namespace eve::editor
