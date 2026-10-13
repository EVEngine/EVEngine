#include "editor/EditorHostProfile.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <span>
#include <utility>
#include "common/Exception.h"
#include "editor/EditorInspector.h"
#include "editor/EditorPersistence.h"
#include "editor/EditorPropertyModel.h"
#include "editor/EditorTransactionService.h"

// EditorHostProfile.cpp
namespace eve::editor {

HostProfile HostProfile::developer() {
    HostProfile profile(HostKind::Developer);
    profile.features_ = HostFeature::ProjectDocuments | HostFeature::SourceAssets | HostFeature::RuntimeWorld |
                        HostFeature::ArbitraryScript | HostFeature::BuildCook | HostFeature::SourceControl |
                        HostFeature::MultiplayerSubmit | HostFeature::UserCreations | HostFeature::DebugOverride;
    profile.allowAllCommands_ = true;
    profile.maxPayloadBytes_  = 4 * 1024 * 1024;
    return profile;
}

HostProfile HostProfile::runtimeBuilder() {
    HostProfile profile(HostKind::RuntimeBuilder);
    profile.features_ = HostFeature::RuntimeWorld | HostFeature::UserCreations;
    return profile;
}

HostProfile HostProfile::automation() {
    HostProfile profile(HostKind::Automation);
    profile.features_        = HostFeature::ProjectDocuments | HostFeature::RuntimeWorld;
    profile.maxPayloadBytes_ = 1024 * 1024;
    return profile;
}

bool HostProfile::hasFeatures(HostFeature required) const { return (features_ & required) == required; }

void HostProfile::allowCommand(CommandId id) {
    if (!id) return;
    deniedCommands_.erase(id);
    allowedCommands_.insert(std::move(id));
}

void HostProfile::allowCommands(std::initializer_list<CommandId> ids) {
    for (const auto &id : ids) allowCommand(id);
}

void HostProfile::denyCommand(const CommandId &id) {
    if (!id) return;
    allowedCommands_.erase(id);
    deniedCommands_.insert(id);
}

bool HostProfile::allowsCommand(const CommandId &id) const {
    if (!id || deniedCommands_.contains(id)) return false;
    return allowAllCommands_ || allowedCommands_.contains(id);
}

}  // namespace eve::editor

// EditorInspector.cpp
namespace eve::editor {

void EditorInspector::clear() {
    fields_.clear();
    pollCursor_ = 0;
}

int EditorInspector::findIndex(const std::string &id) const {
    for (int i = 0; i < static_cast<int>(fields_.size()); ++i) {
        if (fields_[i].id == id) return i;
    }
    return -1;
}

EditorInspector::Field *EditorInspector::find(const std::string &id) {
    int i = findIndex(id);
    return i < 0 ? nullptr : &fields_[i];
}

const EditorInspector::Field *EditorInspector::find(const std::string &id) const {
    int i = findIndex(id);
    return i < 0 ? nullptr : &fields_[i];
}

void EditorInspector::addFloat(const std::string &id, const std::string &label, float value, float minV, float maxV,
                               float step) {
    if (id.empty() || findIndex(id) >= 0) throw Exception("EditorInspector::addFloat: bad id");
    Field f;
    f.kind  = "float";
    f.id    = id;
    f.label = label;
    f.f0    = value;
    f.minV  = minV;
    f.maxV  = maxV;
    f.step  = step;
    fields_.push_back(f);
}

void EditorInspector::addFloat3(const std::string &id, const std::string &label, float x, float y, float z) {
    if (id.empty() || findIndex(id) >= 0) throw Exception("EditorInspector::addFloat3: bad id");
    Field f;
    f.kind  = "float3";
    f.id    = id;
    f.label = label;
    f.f0    = x;
    f.f1    = y;
    f.f2    = z;
    fields_.push_back(f);
}

void EditorInspector::addBool(const std::string &id, const std::string &label, bool value) {
    if (id.empty() || findIndex(id) >= 0) throw Exception("EditorInspector::addBool: bad id");
    Field f;
    f.kind  = "bool";
    f.id    = id;
    f.label = label;
    f.b     = value;
    fields_.push_back(f);
}

void EditorInspector::addString(const std::string &id, const std::string &label, const std::string &value) {
    if (id.empty() || findIndex(id) >= 0) throw Exception("EditorInspector::addString: bad id");
    Field f;
    f.kind  = "string";
    f.id    = id;
    f.label = label;
    f.s     = value;
    fields_.push_back(f);
}

void EditorInspector::addChoice(const std::string &id, const std::string &label, const std::string &choicesCsv,
                                const std::string &selected) {
    if (id.empty() || findIndex(id) >= 0) throw Exception("EditorInspector::addChoice: bad id");
    Field f;
    f.kind    = "choice";
    f.id      = id;
    f.label   = label;
    f.choices = choicesCsv;
    f.s       = selected;
    fields_.push_back(f);
}

std::string EditorInspector::getFieldKind(int index) const {
    if (index < 0 || index >= static_cast<int>(fields_.size()))
        throw Exception("EditorInspector::getFieldKind: bad index");
    return fields_[index].kind;
}
std::string EditorInspector::getFieldId(int index) const {
    if (index < 0 || index >= static_cast<int>(fields_.size()))
        throw Exception("EditorInspector::getFieldId: bad index");
    return fields_[index].id;
}
std::string EditorInspector::getFieldLabel(int index) const {
    if (index < 0 || index >= static_cast<int>(fields_.size()))
        throw Exception("EditorInspector::getFieldLabel: bad index");
    return fields_[index].label;
}

float EditorInspector::getFloat(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "float") throw Exception("EditorInspector::getFloat: missing float field");
    return f->f0;
}

void EditorInspector::setFloat(const std::string &id, float value) {
    Field *f = find(id);
    if (!f || f->kind != "float") throw Exception("EditorInspector::setFloat: missing float field");
    if (f->f0 != value) {
        f->f0    = value;
        f->dirty = true;
    }
}

float EditorInspector::getFloatMin(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "float") throw Exception("EditorInspector::getFloatMin: missing");
    return f->minV;
}
float EditorInspector::getFloatMax(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "float") throw Exception("EditorInspector::getFloatMax: missing");
    return f->maxV;
}
float EditorInspector::getFloatStep(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "float") throw Exception("EditorInspector::getFloatStep: missing");
    return f->step;
}

float EditorInspector::getFloat3X(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "float3") throw Exception("EditorInspector::getFloat3X: missing");
    return f->f0;
}
float EditorInspector::getFloat3Y(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "float3") throw Exception("EditorInspector::getFloat3Y: missing");
    return f->f1;
}
float EditorInspector::getFloat3Z(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "float3") throw Exception("EditorInspector::getFloat3Z: missing");
    return f->f2;
}

void EditorInspector::setFloat3(const std::string &id, float x, float y, float z) {
    Field *f = find(id);
    if (!f || f->kind != "float3") throw Exception("EditorInspector::setFloat3: missing");
    if (f->f0 != x || f->f1 != y || f->f2 != z) {
        f->f0    = x;
        f->f1    = y;
        f->f2    = z;
        f->dirty = true;
    }
}

bool EditorInspector::getBool(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "bool") throw Exception("EditorInspector::getBool: missing");
    return f->b;
}

void EditorInspector::setBool(const std::string &id, bool value) {
    Field *f = find(id);
    if (!f || f->kind != "bool") throw Exception("EditorInspector::setBool: missing");
    if (f->b != value) {
        f->b     = value;
        f->dirty = true;
    }
}

std::string EditorInspector::getString(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "string") throw Exception("EditorInspector::getString: missing");
    return f->s;
}

void EditorInspector::setString(const std::string &id, const std::string &value) {
    Field *f = find(id);
    if (!f || f->kind != "string") throw Exception("EditorInspector::setString: missing");
    if (f->s != value) {
        f->s     = value;
        f->dirty = true;
    }
}

std::string EditorInspector::getChoice(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "choice") throw Exception("EditorInspector::getChoice: missing");
    return f->s;
}

void EditorInspector::setChoice(const std::string &id, const std::string &value) {
    Field *f = find(id);
    if (!f || f->kind != "choice") throw Exception("EditorInspector::setChoice: missing");
    if (f->s != value) {
        f->s     = value;
        f->dirty = true;
    }
}

std::string EditorInspector::getChoicesCsv(const std::string &id) const {
    const Field *f = find(id);
    if (!f || f->kind != "choice") throw Exception("EditorInspector::getChoicesCsv: missing");
    return f->choices;
}

bool EditorInspector::isDirty(const std::string &id) const {
    const Field *f = find(id);
    return f && f->dirty;
}

void EditorInspector::clearDirty(const std::string &id) {
    Field *f = find(id);
    if (f) f->dirty = false;
}

void EditorInspector::clearAllDirty() {
    for (auto &f : fields_) f.dirty = false;
    pollCursor_ = 0;
}

std::string EditorInspector::pollChangedId() {
    const int n = static_cast<int>(fields_.size());
    if (n == 0) return "";
    for (int k = 0; k < n; ++k) {
        int i = (pollCursor_ + k) % n;
        if (fields_[i].dirty) {
            fields_[i].dirty = false;
            pollCursor_      = (i + 1) % n;
            return fields_[i].id;
        }
    }
    return "";
}

}  // namespace eve::editor

// EditorPersistence.cpp
namespace eve::editor {
namespace {

EditorValue operationValue(const DomainOperation &operation) {
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

DomainOperation decodeOperation(const EditorValue &value) {
    DomainOperation operation;
    const auto     *object = value.getIf<EditorValue::Object>();
    if (!object) return operation;
    const auto text = [&](const char *key) {
        auto        found  = object->find(key);
        const auto *result = found == object->end() ? nullptr : found->second.getIf<std::string>();
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
    const auto *flag       = hasInverse == object->end() ? nullptr : hasInverse->second.getIf<bool>();
    operation.hasInverse   = flag && *flag;
    return operation;
}

Result<EditorPersistenceSnapshot> persistenceError(EditorStatus status, const char *rule, std::string message) {
    return eve::editing::failed<EditorPersistenceSnapshot>(status, RuleId(rule), std::move(message));
}

}  // namespace

Result<EditorPersistenceSnapshot> EditorPersistenceAdapter::load() const {
    if (!store_ || resourceUri_.empty())
        return persistenceError(EditorStatus::Rejected, "editor.persistence.invalid",
                                "Persistence store and URI are required");
    auto stored = store_->read(resourceUri_);
    if (!stored.ok()) {
        if (stored.code() == EditorStatus::NotFound) {
            EditorPersistenceSnapshot empty;
            empty.kind = kind_;
            return eve::editing::applied<EditorPersistenceSnapshot>(std::move(empty));
        }
        return Result<EditorPersistenceSnapshot>::failure(stored.status());
    }
    return decode(kind_, stored.value());
}

Result<EditorPersistenceSnapshot> EditorPersistenceAdapter::commit(const EditorPersistenceSnapshot &base,
                                                                   EditorValue                      content,
                                                                   std::vector<DomainOperation>     journal) {
    if (!store_ || base.kind != kind_)
        return persistenceError(EditorStatus::Rejected, "editor.persistence.kind-mismatch",
                                "Persistence checkpoint belongs to another backend kind");
    EditorValue::Array serializedJournal;
    for (const DomainOperation &operation : journal) serializedJournal.push_back(operationValue(operation));
    EditorValue::Object envelope;
    envelope["schemaVersion"] = EditorValue(1);
    envelope["content"]       = content;
    envelope["journal"]       = EditorValue(std::move(serializedJournal));
    auto stored =
        store_->compareAndSwap(resourceUri_, base.revision, base.contentHash, EditorValue(std::move(envelope)));
    if (!stored.ok()) return Result<EditorPersistenceSnapshot>::failure(stored.status());
    return decode(kind_, stored.value());
}

Result<EditorPersistenceSnapshot> EditorPersistenceAdapter::decode(EditorPersistenceKind kind,
                                                                   const StoredDocument &stored) {
    const auto *envelope = stored.content.getIf<EditorValue::Object>();
    if (!envelope)
        return persistenceError(EditorStatus::Failed, "editor.persistence.invalid-envelope",
                                "Persistence checkpoint must be an object");
    const auto  content       = envelope->find("content");
    const auto  journal       = envelope->find("journal");
    const auto *journalValues = journal == envelope->end() ? nullptr : journal->second.getIf<EditorValue::Array>();
    if (content == envelope->end() || !journalValues)
        return persistenceError(EditorStatus::Failed, "editor.persistence.invalid-envelope",
                                "Persistence checkpoint content or journal is missing");
    EditorPersistenceSnapshot result;
    result.kind        = kind;
    result.revision    = stored.revision;
    result.contentHash = stored.contentHash;
    result.content     = content->second;
    for (const EditorValue &value : *journalValues) result.journal.push_back(decodeOperation(value));
    return eve::editing::applied<EditorPersistenceSnapshot>(std::move(result));
}

}  // namespace eve::editor

// EditorPropertyModel.cpp
namespace eve::editor {
namespace {

template <class T>
property_access::WriteResult writeFailure(const Result<T> &result, std::string fallbackCode,
                                          std::string fallbackMessage) {
    if (!result.diagnostics().empty()) {
        const EditorDiagnostic &diagnostic = result.diagnostics().front();
        const RuleId            rule       = eve::editing::diagnosticRule(diagnostic);
        if (!rule.empty() && !diagnostic.message().empty())
            return property_access::WriteResult::reject(rule.value(), diagnostic.message());
        if (!diagnostic.message().empty())
            return property_access::WriteResult::reject(std::move(fallbackCode), diagnostic.message());
    }
    return property_access::WriteResult::reject(std::move(fallbackCode), std::move(fallbackMessage));
}

TransactionId newPropertyTransactionId() {
    static std::atomic<std::uint64_t> entropySequence{1};
    const eve::UuidV7Generator        generator([](std::span<std::uint8_t> bytes) {
        const std::uint64_t seed = entropySequence.fetch_add(1, std::memory_order_relaxed);
        for (std::size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<std::uint8_t>(seed >> ((index % sizeof(seed)) * 8u));
        return true;
    });
    const auto                        generated = generator.generate(std::chrono::system_clock::now());
    if (!generated) {
        // A clock before the UUID epoch is not expected for a live editor, but
        // retain a valid canonical UUID path if a platform clock is malformed.
        const auto epoch = generator.generate(std::chrono::system_clock::time_point{});
        if (!epoch) return TransactionId{};
        using CanonicalUuid = typename TransactionId::Uuid;
        return TransactionId::fromUuid(CanonicalUuid(epoch->bytes()));
    }
    using CanonicalUuid = typename TransactionId::Uuid;
    return TransactionId::fromUuid(CanonicalUuid(generated->bytes()));
}

TargetId selectionTarget(const SelectionSnapshot &selection) {
    if (selection.primary && !selection.primary->target.empty()) return selection.primary->target;
    for (const SelectionItem &item : selection.items)
        if (!item.target.empty()) return item.target;
    return {};
}

EditorStatus editorStatusFor(eve::StatusCode code) {
    switch (code) {
        case eve::StatusCode::Conflict: return EditorStatus::Conflict;
        case eve::StatusCode::NotFound: return EditorStatus::NotFound;
        case eve::StatusCode::Unsupported: return EditorStatus::Unsupported;
        case eve::StatusCode::Cancelled: return EditorStatus::Cancelled;
        case eve::StatusCode::Rejected: return EditorStatus::Rejected;
        case eve::StatusCode::Ok:
        case eve::StatusCode::Applied:
        case eve::StatusCode::NoOp:
        case eve::StatusCode::Pending:
        case eve::StatusCode::Failed: return EditorStatus::Failed;
    }
    return EditorStatus::Failed;
}

Result<eve::Revision> providerRevisionFailure(const eve::Result<eve::Revision> &result) {
    const eve::Status &status = result.status();
    const char        *rule   = "editor.property.current-revision";
    switch (status.code()) {
        case eve::StatusCode::Conflict: rule = "editor.property.revision-conflict"; break;
        case eve::StatusCode::Unsupported: rule = "editor.property.revision-unsupported"; break;
        case eve::StatusCode::NotFound: rule = "editor.property.target"; break;
        default: break;
    }
    return eve::editing::failed<eve::Revision>(editorStatusFor(status.code()), RuleId(rule), status.describe());
}

Result<void> externalRevisionConflict(eve::Revision expected, eve::Revision actual) {
    return eve::editing::failed<void>(EditorStatus::Conflict, RuleId("editor.property.revision-conflict"),
                                      "Property target revision changed from " + std::to_string(expected.value()) +
                                          " to " + std::to_string(actual.value()) + "; refresh/rebase before writing");
}

}  // namespace

struct EditorPropertyModel::ObserverState {
    struct Entry {
        std::uint64_t  id = 0;
        ChangeCallback callback;
    };
    std::uint64_t      nextId = 1;
    std::vector<Entry> entries;
};

EditorPropertyModel::EditorPropertyModel(PropertySchema schema, SelectionSnapshot selection,
                                         const IPropertyProvider *provider, PropertyModelSurface surface,
                                         HostProfile profile, IEditorTransactionBackend *transactionBackend)
    : editorSchema_(std::move(schema)),
      selection_(std::move(selection)),
      provider_(provider),
      surface_(surface),
      profile_(std::move(profile)),
      transactionBackend_(transactionBackend),
      observers_(std::make_shared<ObserverState>()) {
    rebuildSchema();
    const Result<void> initialBinding = bind();
    if (!initialBinding.ok()) {
        // A constructor cannot return a Result. Keep the model unbound so
        // every write fails closed until the caller explicitly refreshes.
    }
}

EditorPropertyModel::~EditorPropertyModel() = default;

void EditorPropertyModel::rebuildSchema() {
    presentationSchema_.typeId  = editorSchema_.typeId;
    presentationSchema_.version = editorSchema_.version;
    presentationSchema_.properties.clear();
    if (surface_ == PropertyModelSurface::Runtime && !profile_.hasFeatures(HostFeature::RuntimeWorld)) return;
    for (const PropertyDescriptor &property : editorSchema_.properties) {
        if (surface_ == PropertyModelSurface::Runtime && (!hasPropertyFlag(property.flags, PropertyFlag::Runtime) ||
                                                          hasPropertyFlag(property.flags, PropertyFlag::EditorOnly)))
            continue;
        presentationSchema_.properties.push_back(toPresentationDescriptor(property));
    }
}

std::optional<eve::Value> EditorPropertyModel::read(const std::string &path) const {
    if (!presentationSchema_.find(path)) return std::nullopt;
    const auto found = cachedProperties_.find(path);
    if (found == cachedProperties_.end() || found->second.state != property_access::PropertyChangeState::Value)
        return std::nullopt;
    return found->second.value;
}

property_access::WriteResult EditorPropertyModel::write(const std::string &path, const eve::Value &value) {
    if (!provider_) return property_access::WriteResult::reject("editor.property.provider", "No property provider");
    const Result<void> revisionCheck = ensureCurrentRevision();
    if (!revisionCheck.ok())
        return writeFailure(revisionCheck, "editor.property.revision", "Property revision is unavailable");
    if (transactionBackend_ && transactionBackend_->active() && pendingPaths_.contains(path))
        return property_access::WriteResult::reject("editor.property.duplicate-path",
                                                    "A property may be written only once per transaction");

    const EditorValue          editorValue = toEditorValue(value);
    Result<PropertyEditIntent> intent      = [&]() {
        if (surface_ == PropertyModelSurface::Runtime) {
            RuntimePropertyPresenter presenter;
            return presenter.editIntent(editorSchema_, selection_, PropertyPath(path), editorValue, profile_);
        }
        DeveloperPropertyPresenter presenter;
        return presenter.editIntent(editorSchema_, selection_, PropertyPath(path), editorValue);
    }();
    if (!intent.ok()) {
        return writeFailure(intent, "editor.property.intent", "Property edit was rejected");
    }

    if (transactionBackend_) {
        Result<DomainOperation> operation =
            provider_->makeSet(selection_, PropertyPath(path), editorValue, PropertySetMode::Absolute);
        if (!operation.ok())
            return writeFailure(operation, "editor.property.operation", "Property operation was rejected");
        if (operation.value().target.empty())
            return property_access::WriteResult::reject("editor.property.target", "Property operation has no target");
        if (operation.value().mergeKey.empty())
            operation.value().mergeKey = intent.value().command.value() + ":" + path;

        bool started = false;
        if (!transactionBackend_->active()) {
            TransactionSpec specification;
            specification.id           = newPropertyTransactionId();
            specification.label        = intent.value().command.value();
            specification.target       = operation.value().target;
            specification.mergeKey     = operation.value().mergeKey;
            specification.baseRevision = targetRevision_.value();
            if (specification.id.empty())
                return property_access::WriteResult::reject("editor.property.transaction-id",
                                                            "Could not allocate a property transaction identity");
            Result<TransactionId> begun = transactionBackend_->begin(std::move(specification));
            if (!begun.ok() || !begun.ok())
                return writeFailure(begun, "editor.property.transaction.begin", "Could not begin property transaction");
            started = true;
        }

        Result<void> appended = transactionBackend_->append(std::move(operation.value()));
        if (!appended.ok()) {
            if (started) {
                Result<void> discarded = transactionBackend_->discard();
                if (!discarded.ok())
                    return writeFailure(discarded, "editor.property.transaction.discard",
                                        "Could not discard rejected property transaction");
            }
            return writeFailure(appended, "editor.property.transaction.append", "Could not stage property operation");
        }
        pendingPaths_.insert(path);

        // An already-open transaction is intentionally left staged for the
        // caller's explicit preview/commit boundary.
        if (!started) return property_access::WriteResult::success();

        Result<EditorDryRunReport> previewed = transactionBackend_->preview();
        if (!previewed.ok() || !previewed.ok()) {
            Result<void> discarded = transactionBackend_->discard();
            pendingPaths_.clear();
            if (!discarded.ok())
                return writeFailure(discarded, "editor.property.transaction.discard",
                                    "Could not discard failed property preview");
            return writeFailure(previewed, "editor.property.transaction.preview",
                                "Property transaction preview was rejected");
        }

        Result<TransactionReceipt> committed = transactionBackend_->commit();
        if (!committed.ok() || !committed.ok())
            // Commit failure is deliberately retained by the backend so the
            // caller can inspect diagnostics, retry, or explicitly discard.
            return writeFailure(committed, "editor.property.transaction.commit", "Property transaction commit failed");
        targetRevision_ = eve::Revision(committed.value().afterRevision);
        pendingPaths_.clear();
        // The mutation already committed. Do not report a successful commit
        // as a rejected write; force an explicit refresh if observation fails.
        if (!refresh().ok()) bound_ = false;
        return property_access::WriteResult::success();
    }

    if (!sink_) return property_access::WriteResult::reject("editor.property.sink", "No command sink is connected");
    // The sink is a compatibility path and may not have an authority that can
    // perform a commit-time CAS. Recheck immediately before handing it the
    // intent; stale compatibility writes must fail closed as well.
    const Result<void> sinkRevisionCheck = ensureCurrentRevision();
    if (!sinkRevisionCheck.ok())
        return writeFailure(sinkRevisionCheck, "editor.property.revision", "Property revision is unavailable");
    const Result<void> applied = sink_(intent.value());
    if (!applied.ok()) return writeFailure(applied, "editor.property.command", "Property command failed");
    // A legacy sink may mutate the provider without returning a receipt. Read
    // the authoritative post-command revision so the next write does not use
    // the pre-command baseline. If observation fails, retain command success
    // and require an explicit refresh before another write.
    if (!refresh().ok()) bound_ = false;
    return property_access::WriteResult::success();
}

Result<void> EditorPropertyModel::setTransactionBackend(IEditorTransactionBackend *backend) {
    if (transactionBackend_ && transactionBackend_->active())
        return eve::editing::failed<void>(
            EditorStatus::Conflict, RuleId("editor.property.active-transaction"),
            "Cannot replace a property transaction backend while a transaction is active");
    if (backend && backend->active())
        return eve::editing::failed<void>(EditorStatus::Conflict, RuleId("editor.property.backend-active"),
                                          "Cannot attach a backend that already has an active transaction");
    transactionBackend_ = backend;
    pendingPaths_.clear();
    return eve::editing::applied<void>();
}

Result<TransactionId> EditorPropertyModel::beginTransaction(std::string label) {
    if (!transactionBackend_)
        return eve::editing::failed<TransactionId>(EditorStatus::Unsupported,
                                                   RuleId("editor.property.transaction-backend"),
                                                   "A transaction backend is not configured");
    if (transactionBackend_->active())
        return eve::editing::failed<TransactionId>(EditorStatus::Conflict, RuleId("editor.property.transaction-active"),
                                                   "A transaction is already active");
    const TargetId target = selectionTarget(selection_);
    if (target.empty())
        return eve::editing::failed<TransactionId>(EditorStatus::Rejected, RuleId("editor.property.target"),
                                                   "An explicit property transaction requires a selected target");
    const Result<void> revisionCheck = ensureCurrentRevision();
    if (!revisionCheck.ok()) {
        return Result<TransactionId>::failure(revisionCheck.status());
    }
    TransactionSpec specification;
    specification.id           = newPropertyTransactionId();
    specification.label        = std::move(label);
    specification.target       = target;
    specification.baseRevision = targetRevision_.value();
    specification.mergeKey     = "editor.property";
    if (specification.id.empty())
        return eve::editing::failed<TransactionId>(EditorStatus::Failed, RuleId("editor.property.transaction-id"),
                                                   "Could not allocate a property transaction identity");
    Result<TransactionId> result = transactionBackend_->begin(std::move(specification));
    if (result.ok()) pendingPaths_.clear();
    return result;
}

Result<EditorDryRunReport> EditorPropertyModel::previewTransaction() {
    if (!transactionBackend_)
        return eve::editing::failed<EditorDryRunReport>(EditorStatus::Unsupported,
                                                        RuleId("editor.property.transaction-backend"),
                                                        "A transaction backend is not configured");
    Result<EditorDryRunReport> result = transactionBackend_->preview();
    if (!result.ok()) return result;
    return result;
}

Result<TransactionReceipt> EditorPropertyModel::commitTransaction() {
    if (!transactionBackend_)
        return eve::editing::failed<TransactionReceipt>(EditorStatus::Unsupported,
                                                        RuleId("editor.property.transaction-backend"),
                                                        "A transaction backend is not configured");
    Result<TransactionReceipt> result = transactionBackend_->commit();
    if (!result.ok()) return result;
    targetRevision_ = eve::Revision(result.value().afterRevision);
    pendingPaths_.clear();
    const Result<void> refreshed = refresh();
    if (!refreshed.ok())
        return Result<TransactionReceipt>::success(result.value(),
                                                   eve::Status(EditorStatus::Applied, refreshed.diagnostics()));
    return result;
}

Result<void> EditorPropertyModel::rollbackTransaction() {
    if (!transactionBackend_)
        return eve::editing::failed<void>(EditorStatus::Unsupported, RuleId("editor.property.transaction-backend"),
                                          "A transaction backend is not configured");
    Result<void> result = transactionBackend_->discard();
    if (result.ok()) pendingPaths_.clear();
    return result;
}

Result<TransactionReceipt> EditorPropertyModel::retryTransaction() {
    if (!transactionBackend_)
        return eve::editing::failed<TransactionReceipt>(EditorStatus::Unsupported,
                                                        RuleId("editor.property.transaction-backend"),
                                                        "A transaction backend is not configured");
    Result<TransactionReceipt> result = transactionBackend_->retry();
    if (!result.ok()) return result;
    targetRevision_ = eve::Revision(result.value().afterRevision);
    pendingPaths_.clear();
    const Result<void> refreshed = refresh();
    if (!refreshed.ok())
        return Result<TransactionReceipt>::success(result.value(),
                                                   eve::Status(EditorStatus::Applied, refreshed.diagnostics()));
    return result;
}

Result<TransactionReceipt> EditorPropertyModel::undo() {
    if (!transactionBackend_)
        return eve::editing::failed<TransactionReceipt>(EditorStatus::Unsupported,
                                                        RuleId("editor.property.transaction-backend"),
                                                        "A transaction backend is not configured");
    Result<TransactionReceipt> result = transactionBackend_->undo();
    if (!result.ok()) return result;
    targetRevision_              = eve::Revision(result.value().afterRevision);
    const Result<void> refreshed = refresh();
    if (!refreshed.ok())
        return Result<TransactionReceipt>::success(result.value(),
                                                   eve::Status(EditorStatus::Applied, refreshed.diagnostics()));
    return result;
}

Result<TransactionReceipt> EditorPropertyModel::redo() {
    if (!transactionBackend_)
        return eve::editing::failed<TransactionReceipt>(EditorStatus::Unsupported,
                                                        RuleId("editor.property.transaction-backend"),
                                                        "A transaction backend is not configured");
    Result<TransactionReceipt> result = transactionBackend_->redo();
    if (!result.ok()) return result;
    targetRevision_              = eve::Revision(result.value().afterRevision);
    const Result<void> refreshed = refresh();
    if (!refreshed.ok())
        return Result<TransactionReceipt>::success(result.value(),
                                                   eve::Status(EditorStatus::Applied, refreshed.diagnostics()));
    return result;
}

property_access::Subscription EditorPropertyModel::subscribe(ChangeCallback callback) {
    const std::uint64_t id = observers_->nextId++;
    observers_->entries.push_back({id, std::move(callback)});
    std::weak_ptr<ObserverState> weak = observers_;
    return property_access::Subscription([weak, id]() {
        if (const auto state = weak.lock())
            std::erase_if(state->entries, [id](const ObserverState::Entry &entry) { return entry.id == id; });
    });
}

Result<eve::Revision> EditorPropertyModel::readProviderRevision() const {
    if (!provider_)
        return eve::editing::failed<eve::Revision>(EditorStatus::Unsupported, RuleId("editor.property.provider"),
                                                   "No property provider is connected");
    eve::Result<eve::Revision> result = provider_->currentRevision(selection_);
    if (!result.ok()) return providerRevisionFailure(result);
    return eve::editing::applied<eve::Revision>(eve::Revision(result.value().value()));
}

Result<void> EditorPropertyModel::ensureCurrentRevision() const {
    const Result<eve::Revision> current = readProviderRevision();
    if (!current.ok()) return Result<void>::failure(current.status());
    if (!bound_)
        return eve::editing::failed<void>(EditorStatus::Conflict, RuleId("editor.property.unbound"),
                                          "Property model is not bound to a provider revision; call refresh/rebase");
    if (current.value() != targetRevision_) return externalRevisionConflict(targetRevision_, current.value());
    return eve::editing::applied<void>();
}

Result<void> EditorPropertyModel::bind() { return refresh(); }

Result<void> EditorPropertyModel::rebase() { return refresh(); }

Result<void> EditorPropertyModel::refresh() {
    if (!provider_)
        return eve::editing::failed<void>(EditorStatus::Unsupported, RuleId("editor.property.provider"),
                                          "No property provider is connected");
    if (transactionBackend_ && transactionBackend_->active())
        return eve::editing::failed<void>(EditorStatus::Conflict, RuleId("editor.property.active-transaction"),
                                          "Cannot refresh or rebase while a property transaction is active");

    const Result<eve::Revision> first = readProviderRevision();
    if (!first.ok()) return Result<void>::failure(first.status());

    std::map<std::string, CachedProperty> nextProperties;
    for (const property_access::PropertyDescriptor &property : presentationSchema_.properties) {
        const PropertyReadResult current = provider_->read(selection_, PropertyPath(property.path));
        if (current.state == PropertyReadState::Error) {
            if (!current.diagnostics.empty())
                return Result<void>::failure(eve::Status(EditorStatus::Failed, current.diagnostics));
            return eve::editing::failed<void>(EditorStatus::Failed, RuleId("editor.property.read"),
                                              "Property provider returned a read error");
        }
        CachedProperty cached;
        switch (current.state) {
            case PropertyReadState::Value:
                cached.state = property_access::PropertyChangeState::Value;
                cached.value = toPresentationValue(current.value);
                break;
            case PropertyReadState::Mixed: cached.state = property_access::PropertyChangeState::Mixed; break;
            case PropertyReadState::Missing: cached.state = property_access::PropertyChangeState::Missing; break;
            case PropertyReadState::Error: break;
        }
        nextProperties.emplace(property.path, std::move(cached));
    }

    const Result<eve::Revision> last = readProviderRevision();
    if (!last.ok()) return Result<void>::failure(last.status());
    if (first.value() != last.value()) return externalRevisionConflict(first.value(), last.value());

    std::vector<property_access::PropertyChange> changes;
    for (const auto &[path, property] : nextProperties) {
        const auto found = cachedProperties_.find(path);
        if (found == cachedProperties_.end() || found->second != property)
            changes.push_back({path, property.value.value_or(eve::Value{}), 0, property.state});
    }

    if (!changes.empty()) {
        const std::uint64_t snapshotRevision = ++revision_;
        for (property_access::PropertyChange &change : changes) change.revision = snapshotRevision;
    }
    cachedProperties_ = std::move(nextProperties);
    targetRevision_   = last.value();
    bound_            = true;
    dispatch(std::move(changes));
    return eve::editing::applied<void>();
}

void EditorPropertyModel::dispatch(std::vector<property_access::PropertyChange> changes) {
    if (changes.empty()) return;
    notificationQueue_.push_back(std::move(changes));
    if (dispatchingNotifications_) return;

    dispatchingNotifications_ = true;
    try {
        while (!notificationQueue_.empty()) {
            std::vector<property_access::PropertyChange> batch = std::move(notificationQueue_.front());
            notificationQueue_.erase(notificationQueue_.begin());
            for (const property_access::PropertyChange &change : batch) {
                const auto observers = observers_->entries;
                for (const ObserverState::Entry &entry : observers)
                    if (entry.callback) entry.callback(change);
            }
        }
        dispatchingNotifications_ = false;
    } catch (...) {
        notificationQueue_.clear();
        dispatchingNotifications_ = false;
        throw;
    }
}

}  // namespace eve::editor
