#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorPersistence.h"

using namespace eve::editor;

TEST_CASE("editor.v2.scene_document_and_savegame_share_one_persistence_contract") {
    MemoryAtomicDocumentStore documentStore;
    MemoryAtomicDocumentStore saveGameStore;
    EditorPersistenceAdapter  document(EditorPersistenceKind::SceneDocument, &documentStore,
                                       "content://World/Park.evscene");
    EditorPersistenceAdapter  saveGame(EditorPersistenceKind::SaveGame, &saveGameStore, "savegame://player-1/park");
    auto                      documentBase = document.load();
    auto                      saveBase     = saveGame.load();
    REQUIRE(documentBase.accepted());
    REQUIRE(saveBase.accepted());

    DomainOperation operation;
    operation.type        = "park.place-tree";
    operation.inverseType = "park.remove-tree";
    operation.target      = TargetId("park-world");
    operation.payload     = EditorValue::Object{{"species", EditorValue("oak")}};
    operation.hasInverse  = true;
    const EditorValue checkpoint(EditorValue::Object{{"treeCount", EditorValue(1)}});
    auto              documentCommit = document.commit(*documentBase.value, checkpoint, {operation});
    auto              saveCommit     = saveGame.commit(*saveBase.value, checkpoint, {operation});
    REQUIRE(documentCommit.accepted());
    REQUIRE(saveCommit.accepted());
    CHECK(documentCommit.value->content == saveCommit.value->content);
    CHECK_EQ(documentCommit.value->journal.size(), saveCommit.value->journal.size());
    CHECK_EQ(documentCommit.value->journal.front().type, saveCommit.value->journal.front().type);

    auto stale = saveGame.commit(*saveBase.value, checkpoint, {operation});
    CHECK_EQ(static_cast<int>(stale.status), static_cast<int>(EditorStatus::Conflict));
}
