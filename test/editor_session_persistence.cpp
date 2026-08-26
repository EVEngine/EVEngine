#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorDiskDocumentStore.h"
#include "editor/EditorSession.h"

using namespace eve::editor;

namespace {

EditorValue versionValue(int version) {
    return EditorValue(EditorValue::Object{{"version", EditorValue(version)}});
}

}  // namespace

TEST_CASE("editor.session.autosavesActiveDocument") {
    MemoryAtomicDocumentStore store;
    DocumentService           documents(&store);
    AutosaveService           autosave(&store, "live-project");
    const DocumentKey         key{DocumentKind::Scene, AssetGuid("live-scene")};
    auto opened = documents.open(key, "Live Scene", "content://World/Live.evscene", versionValue(0));
    REQUIRE(opened.accepted());

    EditorSession session;
    session.setDocumentServices(&documents, &autosave);
    REQUIRE(session.bindDocument(opened.value->id).accepted());
    session.setAutosaveInterval(0.1f);
    session.setExternalPollInterval(0.f);
    REQUIRE(session.editDocument(versionValue(1)).accepted());
    session.update(0.11f);

    CHECK_EQ(session.lastAutosavedRevision(), Revision(1));
    auto draft = autosave.readDraft(opened.value->id);
    REQUIRE(draft.accepted());
    CHECK(draft.value->content == versionValue(1));
    CHECK(documents.snapshot(opened.value->id).value->dirty());
}

TEST_CASE("editor.session.adoptsCleanExternalChangesAndProtectsDirtyEdits") {
    MemoryAtomicDocumentStore store;
    DocumentService           documents(&store);
    const DocumentKey         key{DocumentKind::Scene, AssetGuid("conflict-scene")};
    auto opened = documents.open(key, "Conflict Scene", "content://World/Conflict.evscene", versionValue(0));
    REQUIRE(opened.accepted());

    EditorSession session;
    session.setDocumentServices(&documents);
    REQUIRE(session.bindDocument(opened.value->id).accepted());
    REQUIRE(session.editDocument(versionValue(1)).accepted());
    REQUIRE(session.saveDocument().accepted());

    auto disk = store.read("content://World/Conflict.evscene");
    REQUIRE(disk.accepted());
    REQUIRE(store.compareAndSwap("content://World/Conflict.evscene", disk.value->revision,
                                 disk.value->contentHash, versionValue(2))
                .accepted());
    REQUIRE(session.pollDocumentChanges().accepted());
    CHECK(documents.content(opened.value->id).value == std::optional<EditorValue>(versionValue(2)));
    CHECK(!documents.snapshot(opened.value->id).value->dirty());

    REQUIRE(session.editDocument(versionValue(3)).accepted());
    disk = store.read("content://World/Conflict.evscene");
    REQUIRE(store.compareAndSwap("content://World/Conflict.evscene", disk.value->revision,
                                 disk.value->contentHash, versionValue(4))
                .accepted());
    const auto conflict = session.pollDocumentChanges();
    CHECK_EQ(static_cast<int>(conflict.status), static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(static_cast<int>(conflict.value->state), static_cast<int>(DocumentState::Conflict));
    CHECK(documents.content(opened.value->id).value == std::optional<EditorValue>(versionValue(3)));
}
