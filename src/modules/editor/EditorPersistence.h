#pragma once

#include "editor/EditorDocumentService.h"
#include "editor/EditorProtocol.h"

namespace eve::editor {

/** @brief Persistence target selected by a game/editor host. */
enum class EditorPersistenceKind { SceneDocument, SaveGame };

/** @brief Loaded checkpoint plus the domain operations recorded with it. */
struct EditorPersistenceSnapshot {
    EditorPersistenceKind        kind     = EditorPersistenceKind::SceneDocument;
    Revision                     revision = 0;
    std::string                  contentHash;
    EditorValue                  content;
    std::vector<DomainOperation> journal;
};

/**
 * @brief Maps the same editor transaction output to a document URI or save slot.
 *
 * The injected atomic store is the only backend-specific piece: projects may
 * use DiskAtomicDocumentStore for scene documents and adapt their game save
 * service to IAtomicDocumentStore for savegame slots.
 */
class EditorPersistenceAdapter {
public:
    /**
     * @brief Bind one logical persistence target.
     * @param kind Scene document or savegame semantics.
     * @param store Non-owning atomic backend that outlives this adapter.
     * @param resourceUri Backend-specific stable resource identity.
     */
    EditorPersistenceAdapter(EditorPersistenceKind kind, IAtomicDocumentStore* store, std::string resourceUri)
        : kind_(kind), store_(store), resourceUri_(std::move(resourceUri)) {}

    /** @brief Load the last committed checkpoint and journal. */
    EditorResult<EditorPersistenceSnapshot> load() const;
    /** @brief Atomically commit a checkpoint against its loaded revision/hash. */
    EditorResult<EditorPersistenceSnapshot> commit(const EditorPersistenceSnapshot& base, EditorValue content,
                                                   std::vector<DomainOperation> journal);

private:
    static EditorResult<EditorPersistenceSnapshot> decode(EditorPersistenceKind kind, const StoredDocument& stored);
    EditorPersistenceKind                          kind_;
    IAtomicDocumentStore*                          store_ = nullptr;
    std::string                                    resourceUri_;
};

}  // namespace eve::editor
