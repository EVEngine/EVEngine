#pragma once
#include "common/Export.h"


#include "definitions/editing/DefinitionTarget.h"
#include "editing/EditingProperty.h"

namespace eve::schema {
struct SchemaDefinition;
}

namespace eve::definitions_editing {

using namespace eve::editing;
using EditorValue = eve::editing::Value;
using EditorStatus = eve::editing::Status;
using EditorDiagnostic = eve::editing::Diagnostic;
using editing::Result;

/** @brief Schema-driven property adapter for one definition document. */
class EVENGINE_API_BACKENDS DefinitionSchemaFormTarget final : public IPropertyProvider {
public:
    /** @brief Bind a document and exact immutable schema; both must outlive this adapter. */
    DefinitionSchemaFormTarget(DefinitionDocument* document,
                               const schema::SchemaDefinition* schema);
    /** @brief Current revision. */
    eve::Result<eve::Revision> currentRevision(const SelectionSnapshot& selection) const override;
    /** @brief Schema. */
    PropertySchema schema(const SelectionSnapshot& selection) const override;
    /** @brief Reads . */
    PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const override;
    /** @brief Make set. */
    Result<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                          const EditorValue& value, PropertySetMode mode) const override;
    /** @brief Make reset. */
    Result<DomainOperation> makeReset(const SelectionSnapshot& selection,
                                            const PropertyPath& path) const override;
    /** @brief Diagnose schema/document identity, missing required fields and current value constraints. */
    std::vector<EditorDiagnostic> validate() const;

private:
    bool matches(const SelectionSnapshot& selection) const;
    DefinitionDocument* document_ = nullptr;
    const schema::SchemaDefinition* schema_ = nullptr;
};

}  // namespace eve::definitions_editing
