#pragma once

#include "editor/EditorProtocol.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::editor {

/** @brief Authoring document category used to choose controllers and views. */
enum class DocumentKind {
    Scene,
    Prefab,
    Material,
    MaterialFunction,
    MaterialInstance,
    AnimationClip,
    AnimationGraph,
    ParticleGraph,
    UiDocument,
    Timeline,
    ProjectSettings,
    ImportSettings,
    Generic
};

/** @brief Stable document identity; paths are deliberately excluded. */
struct DocumentKey {
    DocumentKind kind = DocumentKind::Generic;
    AssetGuid    asset;

    auto operator<=>(const DocumentKey&) const = default;
};

/** @brief Lifecycle state visible to workspaces and close coordination. */
enum class DocumentState { Loading, Ready, Saving, Conflict, Failed, Closed };

/** @brief Separate edit, saved and external-store revision clocks. */
struct DocumentRevision {
    Revision edit    = 0;
    Revision saved   = 0;
    Revision disk    = 0;
    Revision preview = 0;
};

/** @brief Immutable document metadata snapshot. */
struct DocumentSnapshot {
    DocumentId                    id;
    DocumentKey                   key;
    std::string                   title;
    std::string                   resourceUri;
    DocumentState                 state = DocumentState::Loading;
    DocumentRevision              revision;
    std::string                   diskContentHash;
    std::vector<StableId>         viewIds;
    std::vector<EditorDiagnostic> diagnostics;

    /** @brief True when the latest edit revision has not been saved. */
    bool dirty() const { return revision.edit != revision.saved; }
};

/** @brief Immutable save request capturing one exact edit revision. */
struct SaveTicket {
    StableId    id;
    DocumentId  document;
    Revision    capturedEditRevision = 0;
    Revision    expectedDiskRevision = 0;
    std::string expectedContentHash;
};

/** @brief Value snapshot returned by an atomic document store. */
struct StoredDocument {
    EditorValue content;
    Revision    revision = 0;
    std::string contentHash;
};

/** @brief Compare-and-swap persistence boundary used by DocumentService. */
class IAtomicDocumentStore {
public:
    virtual ~IAtomicDocumentStore() = default;
    /** @brief Read the latest value for a resource URI. */
    virtual EditorResult<StoredDocument> read(const std::string& resourceUri) const = 0;
    /** @brief Replace a value only when the expected disk revision still matches. */
    virtual EditorResult<StoredDocument> compareAndSwap(const std::string& resourceUri, Revision expectedRevision,
                                                        const std::string& expectedContentHash,
                                                        const EditorValue& content) = 0;
};

/** @brief Deterministic in-memory CAS store suitable for tests and transient tools. */
class MemoryAtomicDocumentStore final : public IAtomicDocumentStore {
public:
    EditorResult<StoredDocument> read(const std::string& resourceUri) const override;
    EditorResult<StoredDocument> compareAndSwap(const std::string& resourceUri, Revision expectedRevision,
                                                const std::string& expectedContentHash,
                                                const EditorValue& content) override;

private:
    std::unordered_map<std::string, StoredDocument> documents_;
};

/**
 * @brief Document coordinator with revision-safe requestSave/executeSave sequencing.
 *
 * The store is non-owning and must outlive this service. A successful old save
 * ticket advances `saved` only to its captured edit revision, preserving dirty
 * state for edits made while the save was pending.
 */
class DocumentService {
public:
    explicit DocumentService(IAtomicDocumentStore* store) : store_(store) {}

    /** @brief Open an asset-backed document, loading existing persisted content when present. */
    EditorResult<DocumentSnapshot> open(DocumentKey key, std::string title, std::string resourceUri,
                                        EditorValue initialContent = {});
    /** @brief Replace the working content using an optional expected edit revision. */
    EditorResult<DocumentSnapshot> edit(const DocumentId& document, EditorValue content,
                                        std::optional<Revision> expectedRevision = std::nullopt);
    /** @brief Capture an exact content/revision pair for a later CAS save. */
    EditorResult<SaveTicket> requestSave(const DocumentId& document);
    /** @brief Execute a captured save ticket without reading newer working content. */
    EditorResult<DocumentSnapshot> executeSave(const SaveTicket& ticket);
    /** @brief Return the current working content snapshot. */
    EditorResult<EditorValue> content(const DocumentId& document) const;
    /** @brief Return one metadata snapshot. */
    EditorResult<DocumentSnapshot> snapshot(const DocumentId& document) const;
    /** @brief Return all open documents in stable id order. */
    std::vector<DocumentSnapshot> documents() const;
    /** @brief Close and discard in-memory state; persisted content is unchanged. */
    EditorResult<void> close(const DocumentId& document);

private:
    struct OpenDocument {
        DocumentSnapshot snapshot;
        EditorValue      content;
    };
    struct PendingSave {
        SaveTicket  ticket;
        EditorValue content;
    };

    static EditorResult<DocumentSnapshot> error(EditorStatus status, const char* rule, std::string message);

    IAtomicDocumentStore*                                                        store_ = nullptr;
    std::unordered_map<DocumentId, OpenDocument, StrongEditorIdHash<DocumentId>> open_;
    std::unordered_map<StableId, PendingSave, StrongEditorIdHash<StableId>>      pendingSaves_;
    std::uint64_t                                                                ticketSequence_ = 0;
};

}  // namespace eve::editor
