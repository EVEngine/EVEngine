#include "editor/EditorDiskDocumentStore.h"

#include "editor/EditorValueJson.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace eve::editor {
namespace {

const EditorValue* member(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

std::string stringMember(const EditorValue& value, const char* key) {
    const EditorValue* field = member(value, key);
    const auto*        text  = field ? field->getIf<std::string>() : nullptr;
    return text ? *text : std::string{};
}

Revision revisionMember(const EditorValue& value, const char* key) {
    const EditorValue* field   = member(value, key);
    const auto*        integer = field ? field->getIf<std::int64_t>() : nullptr;
    return integer && *integer >= 0 ? static_cast<Revision>(*integer) : 0;
}

EditorResult<AutosaveDraft> draftFailure(const char* rule, std::string message) {
    return eve::editing::failed<AutosaveDraft>(EditorStatus::Rejected, RuleId(rule), std::move(message));
}

}  // namespace

DiskAtomicDocumentStore::DiskAtomicDocumentStore(std::filesystem::path projectRoot) {
    std::error_code ec;
    root_ = std::filesystem::weakly_canonical(std::move(projectRoot), ec);
    if (ec) root_.clear();
}

std::filesystem::path DiskAtomicDocumentStore::resolve(const std::string& resourceUri) const {
    if (root_.empty()) return {};
    std::string relative;
    if (resourceUri.rfind("content://", 0) == 0)
        relative = "Content/" + resourceUri.substr(10);
    else if (resourceUri.rfind("autosave://", 0) == 0)
        relative = "Saved/Autosave/" + resourceUri.substr(11);
    else
        return {};
    if (relative.empty()) return {};

    std::filesystem::path candidate(relative);
    if (candidate.is_absolute()) return {};
    for (const auto& part : candidate)
        if (part == "..") return {};

    candidate                          = (root_ / candidate).lexically_normal();
    const std::filesystem::path scoped = candidate.lexically_relative(root_);
    if (scoped.empty()) return {};
    for (const auto& part : scoped)
        if (part == "..") return {};
    return candidate;
}

EditorResult<StoredDocument> DiskAtomicDocumentStore::read(const std::string& resourceUri) const {
    const std::filesystem::path path = resolve(resourceUri);
    if (path.empty()) return failure(EditorStatus::Rejected, "editor.document.invalid-uri", "Rejected resource URI");
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return failure(EditorStatus::NotFound, "editor.document.store-not-found",
                       "Document resource is not persisted: " + resourceUri);
    const std::string         json{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    EditorResult<EditorValue> parsed = editorValueFromJson(json);
    if (!parsed.ok() || !parsed.ok())
        return failure(EditorStatus::Failed, "editor.document.invalid-envelope", "Document JSON envelope is invalid");

    const EditorValue* content       = member(parsed.value(), "content");
    const Revision     revision      = revisionMember(parsed.value(), "revision");
    const auto*        schema        = member(parsed.value(), "schemaVersion");
    const auto*        schemaInteger = schema ? schema->getIf<std::int64_t>() : nullptr;
    if (!content || !schemaInteger || *schemaInteger != 1)
        return failure(EditorStatus::Failed, "editor.document.unsupported-schema",
                       "Document envelope schemaVersion must be 1");

    StoredDocument stored;
    stored.content     = *content;
    stored.revision    = revision;
    stored.contentHash = editorValueContentHash(stored.content);
    return eve::editing::applied<StoredDocument>(std::move(stored));
}

EditorResult<StoredDocument> DiskAtomicDocumentStore::compareAndSwap(const std::string& resourceUri,
                                                                     Revision           expectedRevision,
                                                                     const std::string& expectedContentHash,
                                                                     const EditorValue& content) {
    const std::filesystem::path path = resolve(resourceUri);
    if (path.empty()) return failure(EditorStatus::Rejected, "editor.document.invalid-uri", "Rejected resource URI");

    StoredDocument               current;
    EditorResult<StoredDocument> existing = read(resourceUri);
    if (existing.ok())
        current = existing.value();
    else if (existing.code() != EditorStatus::NotFound)
        return existing;
    else
        current.contentHash.clear();
    if (current.revision != expectedRevision || current.contentHash != expectedContentHash)
        return failure(EditorStatus::Conflict, "editor.document.disk-conflict",
                       "Document content or revision changed before save");

    StoredDocument next;
    next.content     = content;
    next.revision    = current.revision + 1;
    next.contentHash = editorValueContentHash(content);
    EditorValue::Object envelope;
    envelope["schemaVersion"] = EditorValue(1);
    envelope["revision"]      = EditorValue(static_cast<std::int64_t>(next.revision));
    envelope["contentHash"]   = EditorValue(next.contentHash);
    envelope["content"]       = content;

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return failure(EditorStatus::Failed, "editor.document.create-directory", ec.message());
    const std::filesystem::path temporary = path.string() + ".tmp." + std::to_string(++sequence_);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return failure(EditorStatus::Failed, "editor.document.temp-open", "Cannot open temp file");
        output << editorValueToJson(EditorValue(std::move(envelope)));
        output.flush();
        if (!output.good()) {
            output.close();
            std::filesystem::remove(temporary, ec);
            return failure(EditorStatus::Failed, "editor.document.temp-write", "Cannot flush temp file");
        }
    }
    if ((beforeReplace_ && !beforeReplace_(path)) || !replace(temporary, path)) {
        std::filesystem::remove(temporary, ec);
        return failure(EditorStatus::Failed, "editor.document.atomic-replace", "Atomic replacement failed");
    }
    return eve::editing::applied<StoredDocument>(std::move(next));
}

EditorResult<StoredDocument> DiskAtomicDocumentStore::failure(EditorStatus status, const char* rule,
                                                              std::string message) {
    return eve::editing::failed<StoredDocument>(status, RuleId(rule), std::move(message));
}

bool DiskAtomicDocumentStore::replace(const std::filesystem::path& temporary,
                                      const std::filesystem::path& destination) const {
#if defined(_WIN32)
    return MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code ec;
    std::filesystem::rename(temporary, destination, ec);
    return !ec;
#endif
}

std::string AutosaveService::draftUri(const DocumentId& document) const {
    std::string safeDocument = document.value();
    std::replace_if(
        safeDocument.begin(), safeDocument.end(),
        [](unsigned char character) { return !std::isalnum(character) && character != '-' && character != '_'; }, '_');
    return "autosave://" + projectId_ + "/" + safeDocument + ".draft";
}

EditorResult<StoredDocument> AutosaveService::writeDraft(const DocumentSnapshot& snapshot, const EditorValue& content) {
    if (!store_ || snapshot.id.empty())
        return eve::editing::failed<StoredDocument>(EditorStatus::Rejected, RuleId("editor.autosave.invalid"),
                                                   "Autosave store and document are required");
    const std::string                  uri      = draftUri(snapshot.id);
    Revision                           revision = 0;
    std::string                        hash;
    EditorResult<StoredDocument> existing = store_->read(uri);
    if (existing.ok()) {
        revision = existing.value().revision;
        hash     = existing.value().contentHash;
    } else if (existing.code() != EditorStatus::NotFound) {
        return std::move(existing);
    }
    EditorValue::Object draft;
    draft["document"]      = EditorValue(snapshot.id.value());
    draft["baseDiskHash"]  = EditorValue(snapshot.diskContentHash);
    draft["schemaVersion"] = EditorValue(1);
    draft["editRevision"]  = EditorValue(static_cast<std::int64_t>(snapshot.revision.edit));
    draft["content"]       = content;
    return store_->compareAndSwap(uri, revision, hash, EditorValue(std::move(draft)));
}

EditorResult<AutosaveDraft> AutosaveService::readDraft(const DocumentId& document) const {
    if (!store_ || document.empty()) return draftFailure("editor.autosave.invalid", "Autosave store is required");
    const EditorResult<StoredDocument> stored = store_->read(draftUri(document));
    if (!stored.ok()) return EditorResult<AutosaveDraft>::failure(stored.status());
    const EditorValue& root    = stored.value().content;
    const EditorValue* content = member(root, "content");
    if (!content || revisionMember(root, "schemaVersion") != 1)
        return draftFailure("editor.autosave.unsupported-schema", "Autosave draft schemaVersion must be 1");
    AutosaveDraft draft;
    draft.document      = DocumentId(stringMember(root, "document"));
    draft.baseDiskHash  = stringMember(root, "baseDiskHash");
    draft.schemaVersion = 1;
    draft.editRevision  = revisionMember(root, "editRevision");
    draft.content       = *content;
    if (draft.document != document)
        return draftFailure("editor.autosave.document-mismatch", "Autosave draft belongs to another document");
    return eve::editing::applied<AutosaveDraft>(std::move(draft));
}

bool AutosaveService::shouldOfferRecovery(const AutosaveDraft& draft, const DocumentSnapshot& formal) const {
    return draft.document == formal.id && draft.editRevision > formal.revision.saved &&
           draft.baseDiskHash == formal.diskContentHash;
}

}  // namespace eve::editor
