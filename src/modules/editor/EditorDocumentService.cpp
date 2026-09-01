#include "editor/EditorDocumentService.h"
#include "editor/EditorValueJson.h"

#include <algorithm>

namespace eve::editor {

EditorResult<StoredDocument> MemoryAtomicDocumentStore::read(const std::string& resourceUri) const {
    auto found = documents_.find(resourceUri);
    if (found == documents_.end())
        return eve::editing::failed<StoredDocument>(EditorStatus::NotFound, RuleId("editor.document.store-not-found"),
                                                   "Document resource is not persisted: " + resourceUri);
    return eve::editing::applied<StoredDocument>(found->second);
}

EditorResult<StoredDocument> MemoryAtomicDocumentStore::compareAndSwap(const std::string& resourceUri,
                                                                       Revision           expectedRevision,
                                                                       const std::string& expectedContentHash,
                                                                       const EditorValue& content) {
    auto              found       = documents_.find(resourceUri);
    const Revision    current     = found == documents_.end() ? 0 : found->second.revision;
    const std::string currentHash = found == documents_.end() ? std::string{} : found->second.contentHash;
    if (current != expectedRevision || currentHash != expectedContentHash)
        return eve::editing::failed<StoredDocument>(EditorStatus::Conflict, RuleId("editor.document.disk-conflict"),
                                                   "Document store revision changed before save");
    StoredDocument stored;
    stored.content     = content;
    stored.revision    = current + 1;
    stored.contentHash = editorValueContentHash(content);
    documents_.insert_or_assign(resourceUri, stored);
    return eve::editing::applied<StoredDocument>(std::move(stored));
}

EditorResult<DocumentSnapshot> DocumentService::open(DocumentKey key, std::string title, std::string resourceUri,
                                                     EditorValue initialContent) {
    if (!store_ || key.asset.empty() || resourceUri.empty())
        return error(EditorStatus::Rejected, "editor.document.invalid-open",
                     "Document store, asset identity and resource URI are required");
    DocumentId id("document:" + key.asset.value());
    if (open_.contains(id)) return eve::editing::applied<DocumentSnapshot>(open_.at(id).snapshot);

    OpenDocument document;
    document.snapshot.id                = id;
    document.snapshot.key               = std::move(key);
    document.snapshot.title             = std::move(title);
    document.snapshot.resourceUri       = std::move(resourceUri);
    document.snapshot.state             = DocumentState::Ready;
    EditorResult<StoredDocument> stored = store_->read(document.snapshot.resourceUri);
    if (stored.ok()) {
        document.content                  = stored.value().content;
        document.snapshot.revision.disk   = stored.value().revision;
        document.snapshot.revision.edit   = stored.value().revision;
        document.snapshot.revision.saved  = stored.value().revision;
        document.snapshot.diskContentHash = stored.value().contentHash;
    } else if (stored.code() == EditorStatus::NotFound) {
        document.content = std::move(initialContent);
    } else {
        return error(stored.code(), "editor.document.store-read", "Document store could not be read");
    }
    DocumentSnapshot snapshot = document.snapshot;
    open_.emplace(id, std::move(document));
    return eve::editing::applied<DocumentSnapshot>(std::move(snapshot));
}

EditorResult<DocumentSnapshot> DocumentService::edit(const DocumentId& document, EditorValue content,
                                                     std::optional<Revision> expectedRevision) {
    auto found = open_.find(document);
    if (found == open_.end()) return error(EditorStatus::NotFound, "editor.document.not-open", "Document is not open");
    if (expectedRevision && *expectedRevision != found->second.snapshot.revision.edit)
        return error(EditorStatus::Conflict, "editor.document.edit-conflict",
                     "Document edit revision changed before mutation");
    found->second.content = std::move(content);
    ++found->second.snapshot.revision.edit;
    found->second.snapshot.state = DocumentState::Ready;
    return eve::editing::applied<DocumentSnapshot>(found->second.snapshot);
}

EditorResult<SaveTicket> DocumentService::requestSave(const DocumentId& document) {
    auto found = open_.find(document);
    if (found == open_.end())
        return eve::editing::failed<SaveTicket>(EditorStatus::NotFound, RuleId("editor.document.not-open"),
                                               "Document is not open");
    SaveTicket ticket;
    ticket.id                   = StableId(document.value() + ".save." + std::to_string(++ticketSequence_));
    ticket.document             = document;
    ticket.capturedEditRevision = found->second.snapshot.revision.edit;
    ticket.expectedDiskRevision = found->second.snapshot.revision.disk;
    ticket.expectedContentHash  = found->second.snapshot.diskContentHash;
    pendingSaves_.emplace(ticket.id, PendingSave{ticket, found->second.content});
    return eve::editing::applied<SaveTicket>(std::move(ticket));
}

EditorResult<DocumentSnapshot> DocumentService::executeSave(const SaveTicket& ticket) {
    auto pending  = pendingSaves_.find(ticket.id);
    auto document = open_.find(ticket.document);
    if (pending == pendingSaves_.end() || document == open_.end())
        return error(EditorStatus::NotFound, "editor.document.save-ticket-not-found",
                     "Save ticket or document is no longer available");
    document->second.snapshot.state = DocumentState::Saving;
    EditorResult<StoredDocument> stored =
        store_->compareAndSwap(document->second.snapshot.resourceUri, ticket.expectedDiskRevision,
                               ticket.expectedContentHash, pending->second.content);
    pendingSaves_.erase(pending);
    if (!stored.ok()) {
        document->second.snapshot.state =
            stored.code() == EditorStatus::Conflict ? DocumentState::Conflict : DocumentState::Failed;
        document->second.snapshot.diagnostics = stored.diagnostics();
        return EditorResult<DocumentSnapshot>::failure(stored.status());
    }
    document->second.snapshot.revision.disk   = stored.value().revision;
    document->second.snapshot.revision.saved  = ticket.capturedEditRevision;
    document->second.snapshot.diskContentHash = stored.value().contentHash;
    document->second.snapshot.state           = DocumentState::Ready;
    document->second.snapshot.diagnostics.clear();
    return eve::editing::applied<DocumentSnapshot>(document->second.snapshot);
}

EditorResult<EditorValue> DocumentService::content(const DocumentId& document) const {
    auto found = open_.find(document);
    if (found == open_.end())
        return eve::editing::failed<EditorValue>(EditorStatus::NotFound, RuleId("editor.document.not-open"),
                                                "Document is not open");
    return eve::editing::applied<EditorValue>(found->second.content);
}

EditorResult<DocumentSnapshot> DocumentService::snapshot(const DocumentId& document) const {
    auto found = open_.find(document);
    if (found == open_.end()) return error(EditorStatus::NotFound, "editor.document.not-open", "Document is not open");
    return eve::editing::applied<DocumentSnapshot>(found->second.snapshot);
}

std::vector<DocumentSnapshot> DocumentService::documents() const {
    std::vector<DocumentSnapshot> result;
    result.reserve(open_.size());
    for (const auto& [id, document] : open_) {
        (void)id;
        result.push_back(document.snapshot);
    }
    std::sort(result.begin(), result.end(),
              [](const DocumentSnapshot& left, const DocumentSnapshot& right) { return left.id < right.id; });
    return result;
}

EditorResult<DocumentSnapshot> DocumentService::reconcileExternal(const DocumentId& document) {
    auto found = open_.find(document);
    if (found == open_.end()) return error(EditorStatus::NotFound, "editor.document.not-open", "Document is not open");

    EditorResult<StoredDocument> stored = store_->read(found->second.snapshot.resourceUri);
    if (!stored.ok()) {
        const bool expectedMissing = stored.code() == EditorStatus::NotFound &&
                                     found->second.snapshot.revision.disk == 0 &&
                                     found->second.snapshot.diskContentHash.empty();
        if (expectedMissing) return eve::editing::applied<DocumentSnapshot>(found->second.snapshot);
        found->second.snapshot.state = stored.code() == EditorStatus::NotFound ||
                                               stored.code() == EditorStatus::Conflict
                                           ? DocumentState::Conflict
                                           : DocumentState::Failed;
        found->second.snapshot.diagnostics = stored.diagnostics();
        if (found->second.snapshot.diagnostics.empty())
            found->second.snapshot.diagnostics.push_back(eve::editing::ruleDiagnostic(
                eve::DiagnosticCode::NotFound, RuleId("editor.document.external-missing"),
                DiagnosticSeverity::Error, "Persisted document was removed externally"));
        const EditorStatus status = found->second.snapshot.state == DocumentState::Conflict
                                        ? EditorStatus::Conflict
                                        : stored.code();
        return EditorResult<DocumentSnapshot>::failure(eve::Status(status, found->second.snapshot.diagnostics));
    }

    const bool changed = stored.value().revision != found->second.snapshot.revision.disk ||
                         stored.value().contentHash != found->second.snapshot.diskContentHash;
    if (!changed) {
        if (found->second.snapshot.state == DocumentState::Conflict) {
            found->second.snapshot.state = DocumentState::Ready;
            found->second.snapshot.diagnostics.clear();
        }
        return eve::editing::applied<DocumentSnapshot>(found->second.snapshot);
    }
    if (found->second.snapshot.dirty()) {
        found->second.snapshot.state = DocumentState::Conflict;
        found->second.snapshot.diagnostics = {eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::Conflict, RuleId("editor.document.external-conflict"), DiagnosticSeverity::Error,
            "Document changed on disk while the session has unsaved edits")};
        return EditorResult<DocumentSnapshot>::failure(
            eve::Status(EditorStatus::Conflict, found->second.snapshot.diagnostics));
    }

    found->second.content = stored.value().content;
    const Revision synchronizedEdit =
        std::max(found->second.snapshot.revision.edit + 1, stored.value().revision);
    found->second.snapshot.revision.edit   = synchronizedEdit;
    found->second.snapshot.revision.saved  = synchronizedEdit;
    found->second.snapshot.revision.disk   = stored.value().revision;
    found->second.snapshot.diskContentHash = stored.value().contentHash;
    found->second.snapshot.state           = DocumentState::Ready;
    found->second.snapshot.diagnostics.clear();
    return eve::editing::applied<DocumentSnapshot>(found->second.snapshot);
}

EditorResult<void> DocumentService::close(const DocumentId& document) {
    if (!open_.erase(document))
        return eve::editing::failed<void>(EditorStatus::NotFound, RuleId("editor.document.not-open"),
                                         "Document is not open");
    std::erase_if(pendingSaves_, [&](const auto& entry) { return entry.second.ticket.document == document; });
    return eve::editing::applied<void>();
}

EditorResult<DocumentSnapshot> DocumentService::error(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<DocumentSnapshot>(status, RuleId(rule), std::move(message));
}

}  // namespace eve::editor
