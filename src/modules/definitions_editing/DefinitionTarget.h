#pragma once

#include "editing/EditingAuthority.h"
#include "editing/EditableTarget.h"

#include <functional>
#include <string>
#include <vector>

namespace eve::definitions {
class DefinitionRegistry;
}

namespace eve::definitions_editing {

using namespace eve::editing;
using EditorValue = eve::editing::Value;
using EditorStatus = eve::editing::Status;
using EditorDiagnostic = eve::editing::Diagnostic;
template <class T>
using EditorResult = eve::editing::Result<T>;

/** @brief Stable cross-definition reference exposed to picker and validation hosts. */
struct DefinitionReferenceField {
    std::string path;
    std::string type;
    std::string id;
    bool required = true;
};

/** @brief UI-neutral versioned definition asset and cross-reference model. */
class DefinitionDocument : public virtual IEditableTarget, public IDomainOperationTarget {
public:
    using ReferenceResolver = std::function<bool(const std::string& type, const std::string& id)>;
    using SchemaValidator = std::function<std::vector<EditorDiagnostic>(const std::string& type,
                                                                        int version,
                                                                        const std::string& json)>;

    /** @brief Construct an empty JSON-object definition with stable identity. */
    DefinitionDocument(std::string type, std::string id, int version = 1);
    const std::string& targetId() const override { return targetId_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime Valid until this target is destroyed or mutated. */
    void* queryCapability(const CapabilityId&) override { return nullptr; }
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    /** @brief Atomically replace canonical payload text after basic JSON-shape validation. */
    EditorResult<void> setJson(std::string json);
    /** @brief Replace the schema version with a positive value. */
    EditorResult<void> setVersion(int version);
    /** @brief Replace editor-extracted cross-reference fields. */
    EditorResult<void> setReferences(std::vector<DefinitionReferenceField> references);
    /** @brief Plan a reversible top-level field assignment from a schema form. */
    EditorResult<DomainOperation> makeSetField(const std::string& field,
                                               const EditorValue& value) const;
    /** @brief Run schema and cross-reference diagnostics without mutating content. */
    std::vector<EditorDiagnostic> validate(const SchemaValidator& schema,
                                           const ReferenceResolver& references) const;
    /** @brief Capture deterministic content for DocumentService persistence. */
    EditorValue snapshotValue() const;
    /** @brief Atomically load a version-one editor snapshot. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);

    /** @brief Return definition type/schema id. */
    const std::string& type() const { return type_; }
    /** @brief Return stable definition id. */
    const std::string& id() const { return id_; }
    /** @brief Return schema version. */
    int version() const { return version_; }
    /** @brief Return JSON payload text. */
    const std::string& json() const { return json_; }
    /** @brief Return current editor revision. */
    editing::Revision documentRevision() const { return revision_; }

private:
    std::string type_;
    std::string id_;
    std::string targetId_;
    int version_ = 1;
    std::string json_ = "{}";
    std::vector<DefinitionReferenceField> references_;
    editing::Revision revision_ = 0;
    EditRegion dirty_;
};

/** @brief Optional bridge publishing a validated definition to DefinitionRegistry. */
class DefinitionRuntimePublisher {
public:
    /** @brief Insert or replace one definition after editor-side validation. */
    EditorResult<void> publish(const DefinitionDocument& document,
                               definitions::DefinitionRegistry* registry,
                               bool replaceExisting = false) const;
};

}  // namespace eve::definitions_editing
