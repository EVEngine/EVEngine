#pragma once

#include "editor/EditorDocumentService.h"

#include <filesystem>
#include <functional>
#include <string>

namespace eve::editor {

/**
 * @brief Project-rooted JSON document store with sibling-temp atomic replacement.
 *
 * `content://` resolves under Content/ and `autosave://` under Saved/Autosave/.
 * Paths escaping the configured root are rejected. The store is thread-safe at
 * the file-operation level only; callers serialize writes per resource URI.
 */
class DiskAtomicDocumentStore final : public IAtomicDocumentStore {
public:
    /** @brief Create a store rooted at a project directory. @param projectRoot Project root containing Content/. */
    explicit DiskAtomicDocumentStore(std::filesystem::path projectRoot);

    /** @brief Read and validate the latest atomic document envelope. */
    EditorResult<StoredDocument> read(const std::string& resourceUri) const override;
    /** @brief CAS and atomically replace one document envelope on disk. */
    EditorResult<StoredDocument> compareAndSwap(const std::string& resourceUri, Revision expectedRevision,
                                                const std::string& expectedContentHash,
                                                const EditorValue& content) override;

    /** @brief Resolve a logical URI to a canonical project path, or an empty path when rejected. */
    std::filesystem::path resolve(const std::string& resourceUri) const;
    /** @brief Test seam invoked after temp write and before atomic replacement. */
    void setBeforeReplaceHook(std::function<bool(const std::filesystem::path&)> hook) {
        beforeReplace_ = std::move(hook);
    }

private:
    static EditorResult<StoredDocument> failure(EditorStatus status, const char* rule, std::string message);
    bool replace(const std::filesystem::path& temporary, const std::filesystem::path& destination) const;

    std::filesystem::path                             root_;
    std::function<bool(const std::filesystem::path&)> beforeReplace_;
    std::uint64_t                                     sequence_ = 0;
};

/** @brief Persisted draft metadata used to decide whether recovery should be offered. */
struct AutosaveDraft {
    DocumentId    document;
    std::string   baseDiskHash;
    std::uint32_t schemaVersion = 1;
    Revision      editRevision  = 0;
    EditorValue   content;
};

/** @brief Autosave coordinator that never overwrites the formal document URI. */
class AutosaveService {
public:
    /**
     * @brief Create an autosave namespace.
     * @param store Non-owning atomic store that outlives this service.
     * @param projectId Stable project identity used in draft URIs.
     */
    AutosaveService(IAtomicDocumentStore* store, std::string projectId)
        : store_(store), projectId_(std::move(projectId)) {}

    /** @brief Write one revisioned draft under autosave://project/document.draft. */
    EditorResult<StoredDocument> writeDraft(const DocumentSnapshot& snapshot, const EditorValue& content);
    /** @brief Read a draft and validate its schema. */
    EditorResult<AutosaveDraft> readDraft(const DocumentId& document) const;
    /** @brief True when a newer draft differs from the current formal disk base. */
    bool shouldOfferRecovery(const AutosaveDraft& draft, const DocumentSnapshot& formal) const;
    /** @brief Return the stable draft URI for one document. */
    std::string draftUri(const DocumentId& document) const;

private:
    IAtomicDocumentStore* store_ = nullptr;
    std::string           projectId_;
};

}  // namespace eve::editor
