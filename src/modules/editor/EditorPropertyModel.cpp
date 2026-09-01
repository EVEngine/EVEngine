#include "editor/EditorPropertyModel.h"
#include "editor/EditorTransactionService.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <span>
#include <utility>

namespace eve::editor {
namespace {

template <class T>
property_access::WriteResult writeFailure(const EditorResult<T> &result, std::string fallbackCode,
                                          std::string fallbackMessage) {
    if (!result.diagnostics.empty()) {
        const EditorDiagnostic &diagnostic = result.diagnostics.front();
        if (!diagnostic.rule.empty() && !diagnostic.message.empty())
            return property_access::WriteResult::reject(diagnostic.rule.value(), diagnostic.message);
        if (!diagnostic.message.empty())
            return property_access::WriteResult::reject(std::move(fallbackCode), diagnostic.message);
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

template <class T>
EditorResult<T> providerRevisionFailure(const eve::Result<eve::Revision> &result) {
    const eve::Status &status = result.status();
    const char        *rule   = "editor.property.current-revision";
    switch (status.code()) {
        case eve::StatusCode::Conflict: rule = "editor.property.revision-conflict"; break;
        case eve::StatusCode::Unsupported: rule = "editor.property.revision-unsupported"; break;
        case eve::StatusCode::NotFound: rule = "editor.property.target"; break;
        default: break;
    }
    return EditorResult<T>::error(editorStatusFor(status.code()), RuleId(rule), status.describe());
}

EditorResult<void> externalRevisionConflict(eve::Revision expected, eve::Revision actual) {
    return EditorResult<void>::error(EditorStatus::Conflict, RuleId("editor.property.revision-conflict"),
                                     "Property target revision changed from " + std::to_string(expected.value()) +
                                         " to " + std::to_string(actual.value()) + "; refresh/rebase before writing");
}

}  // namespace

struct EditorPropertyModel::ObserverState {
    struct Entry {
        std::uint64_t id = 0;
        ChangeCallback callback;
    };
    std::uint64_t nextId = 1;
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
    const EditorResult<void> initialBinding = bind();
    if (!initialBinding.isAccepted()) {
        // A constructor cannot return a Result. Keep the model unbound so
        // every write fails closed until the caller explicitly refreshes.
    }
}

EditorPropertyModel::~EditorPropertyModel() = default;

void EditorPropertyModel::rebuildSchema() {
    presentationSchema_.typeId = editorSchema_.typeId;
    presentationSchema_.version = editorSchema_.version;
    presentationSchema_.properties.clear();
    for (const PropertyDescriptor &property : editorSchema_.properties) {
        if (surface_ == PropertyModelSurface::Runtime &&
            (!hasPropertyFlag(property.flags, PropertyFlag::Runtime) ||
             hasPropertyFlag(property.flags, PropertyFlag::EditorOnly)))
            continue;
        presentationSchema_.properties.push_back(toPresentationDescriptor(property));
    }
}

std::optional<eve::Value> EditorPropertyModel::read(const std::string &path) const {
    if (!provider_ || !presentationSchema_.find(path)) return std::nullopt;
    const PropertyReadResult result = provider_->read(selection_, PropertyPath(path));
    if (result.state != PropertyReadState::Value) return std::nullopt;
    return toPresentationValue(result.value);
}

property_access::WriteResult EditorPropertyModel::write(const std::string &path, const eve::Value &value) {
    if (!provider_) return property_access::WriteResult::reject("editor.property.provider", "No property provider");
    const EditorResult<void> revisionCheck = ensureCurrentRevision();
    if (!revisionCheck.isAccepted())
        return writeFailure(revisionCheck, "editor.property.revision", "Property revision is unavailable");
    if (transactionBackend_ && transactionBackend_->active() && pendingPaths_.contains(path))
        return property_access::WriteResult::reject("editor.property.duplicate-path",
                                                    "A property may be written only once per transaction");

    const EditorValue                editorValue = toEditorValue(value);
    EditorResult<PropertyEditIntent> intent;
    if (surface_ == PropertyModelSurface::Runtime) {
        RuntimePropertyPresenter presenter;
        intent = presenter.editIntent(editorSchema_, selection_, PropertyPath(path), editorValue, profile_);
    } else {
        DeveloperPropertyPresenter presenter;
        intent = presenter.editIntent(editorSchema_, selection_, PropertyPath(path), editorValue);
    }
    if (!intent.isAccepted() || !intent.value) {
        return writeFailure(intent, "editor.property.intent", "Property edit was rejected");
    }

    if (transactionBackend_) {
        EditorResult<DomainOperation> operation =
            provider_->makeSet(selection_, PropertyPath(path), editorValue, PropertySetMode::Absolute);
        if (!operation.isAccepted() || !operation.value)
            return writeFailure(operation, "editor.property.operation", "Property operation was rejected");
        if (operation.value->target.empty())
            return property_access::WriteResult::reject("editor.property.target", "Property operation has no target");
        if (operation.value->mergeKey.empty()) operation.value->mergeKey = intent.value->command.value() + ":" + path;

        bool started = false;
        if (!transactionBackend_->active()) {
            TransactionSpec specification;
            specification.id           = newPropertyTransactionId();
            specification.label        = intent.value->command.value();
            specification.target       = operation.value->target;
            specification.mergeKey     = operation.value->mergeKey;
            specification.baseRevision = targetRevision_.value();
            if (specification.id.empty())
                return property_access::WriteResult::reject("editor.property.transaction-id",
                                                            "Could not allocate a property transaction identity");
            EditorResult<TransactionId> begun = transactionBackend_->begin(std::move(specification));
            if (!begun.isAccepted() || !begun.value)
                return writeFailure(begun, "editor.property.transaction.begin", "Could not begin property transaction");
            started = true;
        }

        EditorResult<void> appended = transactionBackend_->append(std::move(*operation.value));
        if (!appended.isAccepted()) {
            if (started) {
                EditorResult<void> discarded = transactionBackend_->discard();
                if (!discarded.isAccepted())
                    return writeFailure(discarded, "editor.property.transaction.discard",
                                        "Could not discard rejected property transaction");
            }
            return writeFailure(appended, "editor.property.transaction.append", "Could not stage property operation");
        }
        pendingPaths_.insert(path);

        // An already-open transaction is intentionally left staged for the
        // caller's explicit preview/commit boundary.
        if (!started) return property_access::WriteResult::success();

        EditorResult<EditorDryRunReport> previewed = transactionBackend_->preview();
        if (!previewed.isAccepted() || !previewed.value) {
            EditorResult<void> discarded = transactionBackend_->discard();
            pendingPaths_.clear();
            if (!discarded.isAccepted())
                return writeFailure(discarded, "editor.property.transaction.discard",
                                    "Could not discard failed property preview");
            return writeFailure(previewed, "editor.property.transaction.preview",
                                "Property transaction preview was rejected");
        }

        EditorResult<TransactionReceipt> committed = transactionBackend_->commit();
        if (!committed.isAccepted() || !committed.value)
            // Commit failure is deliberately retained by the backend so the
            // caller can inspect diagnostics, retry, or explicitly discard.
            return writeFailure(committed, "editor.property.transaction.commit", "Property transaction commit failed");
        targetRevision_ = eve::Revision(committed.value->afterRevision);
        pendingPaths_.clear();
        // The mutation already committed. Do not report a successful commit
        // as a rejected write; force an explicit refresh if observation fails.
        if (!refresh().isAccepted()) bound_ = false;
        return property_access::WriteResult::success();
    }

    if (!sink_) return property_access::WriteResult::reject("editor.property.sink", "No command sink is connected");
    // The sink is a compatibility path and may not have an authority that can
    // perform a commit-time CAS. Recheck immediately before handing it the
    // intent; stale compatibility writes must fail closed as well.
    const EditorResult<void> sinkRevisionCheck = ensureCurrentRevision();
    if (!sinkRevisionCheck.isAccepted())
        return writeFailure(sinkRevisionCheck, "editor.property.revision", "Property revision is unavailable");
    const EditorResult<void> applied = sink_(*intent.value);
    if (!applied.isAccepted()) return writeFailure(applied, "editor.property.command", "Property command failed");
    // A legacy sink may mutate the provider without returning a receipt. Read
    // the authoritative post-command revision so the next write does not use
    // the pre-command baseline. If observation fails, retain command success
    // and require an explicit refresh before another write.
    if (!refresh().isAccepted()) bound_ = false;
    return property_access::WriteResult::success();
}

EditorResult<void> EditorPropertyModel::setTransactionBackend(IEditorTransactionBackend *backend) {
    if (transactionBackend_ && transactionBackend_->active())
        return EditorResult<void>::error(EditorStatus::Conflict, RuleId("editor.property.active-transaction"),
                                         "Cannot replace a property transaction backend while a transaction is active");
    if (backend && backend->active())
        return EditorResult<void>::error(EditorStatus::Conflict, RuleId("editor.property.backend-active"),
                                         "Cannot attach a backend that already has an active transaction");
    transactionBackend_ = backend;
    pendingPaths_.clear();
    return EditorResult<void>::applied();
}

EditorResult<TransactionId> EditorPropertyModel::beginTransaction(std::string label) {
    if (!transactionBackend_)
        return EditorResult<TransactionId>::error(EditorStatus::Unsupported,
                                                  RuleId("editor.property.transaction-backend"),
                                                  "A transaction backend is not configured");
    if (transactionBackend_->active())
        return EditorResult<TransactionId>::error(EditorStatus::Conflict, RuleId("editor.property.transaction-active"),
                                                  "A transaction is already active");
    const TargetId target = selectionTarget(selection_);
    if (target.empty())
        return EditorResult<TransactionId>::error(EditorStatus::Rejected, RuleId("editor.property.target"),
                                                  "An explicit property transaction requires a selected target");
    const EditorResult<void> revisionCheck = ensureCurrentRevision();
    if (!revisionCheck.isAccepted()) {
        EditorResult<TransactionId> result;
        result.status      = revisionCheck.status;
        result.diagnostics = revisionCheck.diagnostics;
        return result;
    }
    TransactionSpec specification;
    specification.id           = newPropertyTransactionId();
    specification.label        = std::move(label);
    specification.target       = target;
    specification.baseRevision = targetRevision_.value();
    specification.mergeKey     = "editor.property";
    if (specification.id.empty())
        return EditorResult<TransactionId>::error(EditorStatus::Failed, RuleId("editor.property.transaction-id"),
                                                  "Could not allocate a property transaction identity");
    EditorResult<TransactionId> result = transactionBackend_->begin(std::move(specification));
    if (result.isAccepted() && result.value) pendingPaths_.clear();
    return result;
}

EditorResult<EditorDryRunReport> EditorPropertyModel::previewTransaction() {
    if (!transactionBackend_)
        return EditorResult<EditorDryRunReport>::error(EditorStatus::Unsupported,
                                                       RuleId("editor.property.transaction-backend"),
                                                       "A transaction backend is not configured");
    EditorResult<EditorDryRunReport> result = transactionBackend_->preview();
    if (!result.isAccepted()) return result;
    return result;
}

EditorResult<TransactionReceipt> EditorPropertyModel::commitTransaction() {
    if (!transactionBackend_)
        return EditorResult<TransactionReceipt>::error(EditorStatus::Unsupported,
                                                       RuleId("editor.property.transaction-backend"),
                                                       "A transaction backend is not configured");
    EditorResult<TransactionReceipt> result = transactionBackend_->commit();
    if (!result.isAccepted() || !result.value) return result;
    targetRevision_ = eve::Revision(result.value->afterRevision);
    pendingPaths_.clear();
    const EditorResult<void> refreshed = refresh();
    if (!refreshed.isAccepted())
        result.diagnostics.insert(result.diagnostics.end(), refreshed.diagnostics.begin(), refreshed.diagnostics.end());
    return result;
}

EditorResult<void> EditorPropertyModel::rollbackTransaction() {
    if (!transactionBackend_)
        return EditorResult<void>::error(EditorStatus::Unsupported, RuleId("editor.property.transaction-backend"),
                                         "A transaction backend is not configured");
    EditorResult<void> result = transactionBackend_->discard();
    if (result.isAccepted()) pendingPaths_.clear();
    return result;
}

EditorResult<TransactionReceipt> EditorPropertyModel::retryTransaction() {
    if (!transactionBackend_)
        return EditorResult<TransactionReceipt>::error(EditorStatus::Unsupported,
                                                       RuleId("editor.property.transaction-backend"),
                                                       "A transaction backend is not configured");
    EditorResult<TransactionReceipt> result = transactionBackend_->retry();
    if (!result.isAccepted() || !result.value) return result;
    targetRevision_ = eve::Revision(result.value->afterRevision);
    pendingPaths_.clear();
    const EditorResult<void> refreshed = refresh();
    if (!refreshed.isAccepted())
        result.diagnostics.insert(result.diagnostics.end(), refreshed.diagnostics.begin(), refreshed.diagnostics.end());
    return result;
}

EditorResult<TransactionReceipt> EditorPropertyModel::undo() {
    if (!transactionBackend_)
        return EditorResult<TransactionReceipt>::error(EditorStatus::Unsupported,
                                                       RuleId("editor.property.transaction-backend"),
                                                       "A transaction backend is not configured");
    EditorResult<TransactionReceipt> result = transactionBackend_->undo();
    if (!result.isAccepted() || !result.value) return result;
    targetRevision_                    = eve::Revision(result.value->afterRevision);
    const EditorResult<void> refreshed = refresh();
    if (!refreshed.isAccepted())
        result.diagnostics.insert(result.diagnostics.end(), refreshed.diagnostics.begin(), refreshed.diagnostics.end());
    return result;
}

EditorResult<TransactionReceipt> EditorPropertyModel::redo() {
    if (!transactionBackend_)
        return EditorResult<TransactionReceipt>::error(EditorStatus::Unsupported,
                                                       RuleId("editor.property.transaction-backend"),
                                                       "A transaction backend is not configured");
    EditorResult<TransactionReceipt> result = transactionBackend_->redo();
    if (!result.isAccepted() || !result.value) return result;
    targetRevision_                    = eve::Revision(result.value->afterRevision);
    const EditorResult<void> refreshed = refresh();
    if (!refreshed.isAccepted())
        result.diagnostics.insert(result.diagnostics.end(), refreshed.diagnostics.begin(), refreshed.diagnostics.end());
    return result;
}

property_access::Subscription EditorPropertyModel::subscribe(ChangeCallback callback) {
    const std::uint64_t id = observers_->nextId++;
    observers_->entries.push_back({id, std::move(callback)});
    std::weak_ptr<ObserverState> weak = observers_;
    return property_access::Subscription([weak, id]() {
        if (const auto state = weak.lock())
            std::erase_if(state->entries,
                          [id](const ObserverState::Entry &entry) { return entry.id == id; });
    });
}

EditorResult<eve::Revision> EditorPropertyModel::readProviderRevision() const {
    if (!provider_)
        return EditorResult<eve::Revision>::error(EditorStatus::Unsupported, RuleId("editor.property.provider"),
                                                  "No property provider is connected");
    eve::Result<eve::Revision> result = provider_->currentRevision(selection_);
    if (!result.ok()) return providerRevisionFailure<eve::Revision>(result);
    return EditorResult<eve::Revision>::applied(eve::Revision(result.value().value()));
}

EditorResult<void> EditorPropertyModel::ensureCurrentRevision() const {
    const EditorResult<eve::Revision> current = readProviderRevision();
    if (!current.isAccepted() || !current.value) {
        EditorResult<void> result;
        result.status      = current.status;
        result.diagnostics = current.diagnostics;
        return result;
    }
    if (!bound_)
        return EditorResult<void>::error(EditorStatus::Conflict, RuleId("editor.property.unbound"),
                                         "Property model is not bound to a provider revision; call refresh/rebase");
    if (*current.value != targetRevision_) return externalRevisionConflict(targetRevision_, *current.value);
    return EditorResult<void>::applied();
}

EditorResult<void> EditorPropertyModel::bind() { return refresh(); }

EditorResult<void> EditorPropertyModel::rebase() { return refresh(); }

EditorResult<void> EditorPropertyModel::refresh() {
    if (!provider_)
        return EditorResult<void>::error(EditorStatus::Unsupported, RuleId("editor.property.provider"),
                                         "No property provider is connected");
    if (transactionBackend_ && transactionBackend_->active())
        return EditorResult<void>::error(EditorStatus::Conflict, RuleId("editor.property.active-transaction"),
                                         "Cannot refresh or rebase while a property transaction is active");

    const EditorResult<eve::Revision> first = readProviderRevision();
    if (!first.isAccepted() || !first.value) {
        EditorResult<void> result;
        result.status      = first.status;
        result.diagnostics = first.diagnostics;
        return result;
    }

    std::map<std::string, eve::Value> nextValues;
    for (const property_access::PropertyDescriptor &property : presentationSchema_.properties) {
        const PropertyReadResult current = provider_->read(selection_, PropertyPath(property.path));
        if (current.state == PropertyReadState::Error) {
            if (!current.diagnostics.empty()) {
                EditorResult<void> result;
                result.status      = EditorStatus::Failed;
                result.diagnostics = current.diagnostics;
                return result;
            }
            return EditorResult<void>::error(EditorStatus::Failed, RuleId("editor.property.read"),
                                             "Property provider returned a read error");
        }
        if (current.state == PropertyReadState::Value)
            nextValues.emplace(property.path, toPresentationValue(current.value));
    }

    const EditorResult<eve::Revision> last = readProviderRevision();
    if (!last.isAccepted() || !last.value) {
        EditorResult<void> result;
        result.status      = last.status;
        result.diagnostics = last.diagnostics;
        return result;
    }
    if (*first.value != *last.value) return externalRevisionConflict(*first.value, *last.value);

    targetRevision_ = *last.value;
    bound_          = true;
    for (const auto &[path, value] : nextValues) {
        const auto found = cachedValues_.find(path);
        if (found == cachedValues_.end() || found->second != value) emit(path, value);
    }
    return EditorResult<void>::applied();
}

void EditorPropertyModel::emit(const std::string &path, const eve::Value &value) {
    cachedValues_[path] = value;
    const property_access::PropertyChange change{path, value, ++revision_};
    const auto snapshot = observers_->entries;
    for (const ObserverState::Entry &entry : snapshot)
        if (entry.callback) entry.callback(change);
}

}  // namespace eve::editor
