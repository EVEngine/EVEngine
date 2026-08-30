#include "editor/EditorPersistence.h"

namespace eve::editor {
namespace {

EditorValue operationValue(const DomainOperation& operation) {
    EditorValue::Object value;
    value["type"]        = EditorValue(operation.type);
    value["inverseType"] = EditorValue(operation.inverseType);
    value["target"]      = EditorValue(operation.target.value());
    value["payload"]     = operation.payload;
    value["inverse"]     = operation.inverse;
    value["hasInverse"]  = EditorValue(operation.hasInverse);
    value["mergeKey"]    = EditorValue(operation.mergeKey);
    return EditorValue(std::move(value));
}

DomainOperation decodeOperation(const EditorValue& value) {
    DomainOperation operation;
    const auto*     object = value.getIf<EditorValue::Object>();
    if (!object) return operation;
    const auto text = [&](const char* key) {
        auto        found  = object->find(key);
        const auto* result = found == object->end() ? nullptr : found->second.getIf<std::string>();
        return result ? *result : std::string{};
    };
    operation.type        = text("type");
    operation.inverseType = text("inverseType");
    operation.target      = TargetId(text("target"));
    operation.mergeKey    = text("mergeKey");
    auto payload          = object->find("payload");
    if (payload != object->end()) operation.payload = payload->second;
    auto inverse = object->find("inverse");
    if (inverse != object->end()) operation.inverse = inverse->second;
    auto        hasInverse = object->find("hasInverse");
    const auto* flag       = hasInverse == object->end() ? nullptr : hasInverse->second.getIf<bool>();
    operation.hasInverse   = flag && *flag;
    return operation;
}

EditorResult<EditorPersistenceSnapshot> persistenceError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<EditorPersistenceSnapshot>::error(status, RuleId(rule), std::move(message));
}

}  // namespace

EditorResult<EditorPersistenceSnapshot> EditorPersistenceAdapter::load() const {
    if (!store_ || resourceUri_.empty())
        return persistenceError(EditorStatus::Rejected, "editor.persistence.invalid",
                                "Persistence store and URI are required");
    auto stored = store_->read(resourceUri_);
    if (!stored.isAccepted() || !stored.value) {
        if (stored.status == EditorStatus::NotFound) {
            EditorPersistenceSnapshot empty;
            empty.kind = kind_;
            return EditorResult<EditorPersistenceSnapshot>::applied(std::move(empty));
        }
        EditorResult<EditorPersistenceSnapshot> result;
        result.status      = stored.status;
        result.diagnostics = std::move(stored.diagnostics);
        return result;
    }
    return decode(kind_, *stored.value);
}

EditorResult<EditorPersistenceSnapshot> EditorPersistenceAdapter::commit(const EditorPersistenceSnapshot& base,
                                                                         EditorValue                      content,
                                                                         std::vector<DomainOperation>     journal) {
    if (!store_ || base.kind != kind_)
        return persistenceError(EditorStatus::Rejected, "editor.persistence.kind-mismatch",
                                "Persistence checkpoint belongs to another backend kind");
    EditorValue::Array serializedJournal;
    for (const DomainOperation& operation : journal) serializedJournal.push_back(operationValue(operation));
    EditorValue::Object envelope;
    envelope["schemaVersion"] = EditorValue(1);
    envelope["content"]       = content;
    envelope["journal"]       = EditorValue(std::move(serializedJournal));
    auto stored =
        store_->compareAndSwap(resourceUri_, base.revision, base.contentHash, EditorValue(std::move(envelope)));
    if (!stored.isAccepted() || !stored.value) {
        EditorResult<EditorPersistenceSnapshot> result;
        result.status      = stored.status;
        result.diagnostics = std::move(stored.diagnostics);
        return result;
    }
    return decode(kind_, *stored.value);
}

EditorResult<EditorPersistenceSnapshot> EditorPersistenceAdapter::decode(EditorPersistenceKind kind,
                                                                         const StoredDocument& stored) {
    const auto* envelope = stored.content.getIf<EditorValue::Object>();
    if (!envelope)
        return persistenceError(EditorStatus::Failed, "editor.persistence.invalid-envelope",
                                "Persistence checkpoint must be an object");
    const auto  content       = envelope->find("content");
    const auto  journal       = envelope->find("journal");
    const auto* journalValues = journal == envelope->end() ? nullptr : journal->second.getIf<EditorValue::Array>();
    if (content == envelope->end() || !journalValues)
        return persistenceError(EditorStatus::Failed, "editor.persistence.invalid-envelope",
                                "Persistence checkpoint content or journal is missing");
    EditorPersistenceSnapshot result;
    result.kind        = kind;
    result.revision    = stored.revision;
    result.contentHash = stored.contentHash;
    result.content     = content->second;
    for (const EditorValue& value : *journalValues) result.journal.push_back(decodeOperation(value));
    return EditorResult<EditorPersistenceSnapshot>::applied(std::move(result));
}

}  // namespace eve::editor
