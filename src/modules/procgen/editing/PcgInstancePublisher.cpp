#include "procgen/editing/PcgInstancePublisher.h"

#include "editing/EditingResult.h"

namespace eve::procgen_editing {
namespace {

EditorResult<editing::DomainOperation> failOp(const char* rule, std::string message) {
    return eve::editing::rejected<editing::DomainOperation>(editing::RuleId(rule), std::move(message));
}

EditorResult<void> failVoid(const char* rule, std::string message) {
    return eve::editing::rejected<void>(editing::RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

std::string stringField(const EditorValue& value, const char* key) {
    const EditorValue* found = field(value, key);
    if (!found) return {};
    if (const auto* text = found->getIf<std::string>()) return *text;
    return {};
}

std::string statusText(const eve::Status& status) {
    if (const auto* diagnostic = status.primaryDiagnostic()) return std::string(diagnostic->message());
    if (!status.diagnostics().empty()) return std::string(status.diagnostics().front().message());
    return "procgen operation failed";
}

}  // namespace

EditorResult<editing::DomainOperation> PcgInstancePublisher::planPublish(const std::string& batchId,
                                                                         const std::string& assetAttribute,
                                                                         const std::string& defaultAsset) const {
    if (batchId.empty()) return failOp("editor.pcg.publish.batch-id", "publish requires a non-empty batch id");

    editing::DomainOperation operation;
    operation.type        = kPublishType;
    operation.inverseType = kRemoveType;
    operation.hasInverse  = true;
    operation.mergeKey    = std::string("pcg-instances:") + batchId;

    EditorValue::Object payload;
    payload["batchId"]        = EditorValue(batchId);
    payload["assetAttribute"] = EditorValue(assetAttribute.empty() ? std::string("mesh") : assetAttribute);
    payload["defaultAsset"]   = EditorValue(defaultAsset);
    operation.payload         = EditorValue(std::move(payload));

    EditorValue::Object inverse;
    inverse["batchId"] = EditorValue(batchId);
    operation.inverse  = EditorValue(std::move(inverse));
    return eve::editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> PcgInstancePublisher::planRemove(const std::string& batchId) const {
    if (batchId.empty()) return failOp("editor.pcg.remove.batch-id", "remove requires a non-empty batch id");

    editing::DomainOperation operation;
    operation.type       = kRemoveType;
    operation.hasInverse = false;
    operation.mergeKey   = std::string("pcg-instances:") + batchId;
    EditorValue::Object payload;
    payload["batchId"] = EditorValue(batchId);
    operation.payload  = EditorValue(std::move(payload));
    return eve::editing::applied(std::move(operation));
}

EditorResult<void> PcgInstancePublisher::applyPublish(procgen::Procgen& procgen,
                                                      const editing::DomainOperation& operation,
                                                      procgen::ProcgenPointSetHandleRef points) const {
    if (operation.type != kPublishType)
        return failVoid("editor.pcg.publish.type", "applyPublish requires a publishInstances operation");
    const std::string batchId = stringField(operation.payload, "batchId");
    if (batchId.empty()) return failVoid("editor.pcg.publish.batch-id", "publish payload is missing batchId");
    const std::string assetAttribute = stringField(operation.payload, "assetAttribute");
    const std::string defaultAsset   = stringField(operation.payload, "defaultAsset");
    auto published =
        procgen.publishInstances(batchId, points, assetAttribute.empty() ? "mesh" : assetAttribute, defaultAsset);
    if (!published.ok()) return failVoid("editor.pcg.publish.failed", statusText(published.status()));
    return eve::editing::applied();
}

EditorResult<void> PcgInstancePublisher::apply(procgen::Procgen& procgen,
                                               const editing::DomainOperation& operation) const {
    editing::DomainOperation removeOp = operation;
    if (operation.type == kPublishType && operation.hasInverse) {
        removeOp.type    = kRemoveType;
        removeOp.payload = operation.inverse;
    }
    if (removeOp.type != kRemoveType)
        return failVoid("editor.pcg.operation", "unsupported PCG instance operation type");
    const std::string batchId = stringField(removeOp.payload, "batchId");
    if (batchId.empty()) return failVoid("editor.pcg.remove.batch-id", "remove payload is missing batchId");
    auto removed = procgen.removeInstances(batchId);
    if (!removed.ok()) return failVoid("editor.pcg.remove.failed", statusText(removed.status()));
    return eve::editing::applied();
}

}  // namespace eve::procgen_editing
