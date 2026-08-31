#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorDiskDocumentStore.h"
#include "editor/EditorValueJson.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace eve::editor;

namespace {

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() /
           ("eve_editor_disk_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

EditorValue versionValue(int version) { return EditorValue(EditorValue::Object{{"version", EditorValue(version)}}); }

}  // namespace

TEST_CASE("editor.v2.disk_document_atomic_conflict_and_autosave") {
    const std::filesystem::path root = testRoot();
    std::filesystem::create_directories(root);
    DiskAtomicDocumentStore store(root);
    CHECK(store.resolve("content://World/Park.evscene").generic_string() ==
          (root / "Content" / "World" / "Park.evscene").generic_string());
    CHECK(store.resolve("content://../outside.evscene").empty());

    DocumentService   documents(&store);
    const DocumentKey key{DocumentKind::Scene, AssetGuid("park-disk")};
    auto              opened = documents.open(key, "Park", "content://World/Park.evscene", versionValue(0));
    REQUIRE(opened.isAccepted());
    const DocumentId id = opened.value->id;
    REQUIRE(documents.edit(id, versionValue(1)).isAccepted());
    const auto firstTicket = documents.requestSave(id);
    REQUIRE(firstTicket.isAccepted());
    REQUIRE(documents.executeSave(*firstTicket.value).isAccepted());

    const std::filesystem::path path = store.resolve("content://World/Park.evscene");
    CHECK(std::filesystem::is_regular_file(path));
    const auto savedOne = store.read("content://World/Park.evscene");
    REQUIRE(savedOne.isAccepted());
    CHECK(savedOne.value->content == versionValue(1));

    // A timestamp-only external touch does not create a false conflict.
    std::error_code ec;
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ec);
    CHECK(!ec);
    REQUIRE(documents.edit(id, versionValue(2)).isAccepted());
    auto sameContentBase = documents.requestSave(id);
    REQUIRE(documents.executeSave(*sameContentBase.value).isAccepted());

    // Manual external content mutation with a stale envelope revision is still
    // detected because CAS validates the computed disk content hash.
    std::ifstream     input(path, std::ios::binary);
    const std::string json{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    auto              envelope = editorValueFromJson(json);
    REQUIRE(envelope.isAccepted());
    auto* envelopeObject = envelope.value->getIf<EditorValue::Object>();
    REQUIRE(envelopeObject != nullptr);
    (*envelopeObject)["content"] = versionValue(99);
    input.close();
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << editorValueToJson(*envelope.value);
    output.close();

    REQUIRE(documents.edit(id, versionValue(3)).isAccepted());
    auto       stale    = documents.requestSave(id);
    const auto conflict = documents.executeSave(*stale.value);
    CHECK(static_cast<int>(conflict.status) == static_cast<int>(EditorStatus::Conflict));
    CHECK(static_cast<int>(conflict.value->state) == static_cast<int>(DocumentState::Conflict));

    // A failed atomic replace leaves the previously persisted file intact.
    DiskAtomicDocumentStore failureStore(root);
    const auto              beforeFailure = failureStore.read("content://World/Park.evscene");
    REQUIRE(beforeFailure.isAccepted());
    failureStore.setBeforeReplaceHook([](const std::filesystem::path&) { return false; });
    const auto failed = failureStore.compareAndSwap("content://World/Park.evscene", beforeFailure.value->revision,
                                                    beforeFailure.value->contentHash, versionValue(4));
    CHECK(static_cast<int>(failed.status) == static_cast<int>(EditorStatus::Failed));
    const auto afterFailure = store.read("content://World/Park.evscene");
    REQUIRE(afterFailure.isAccepted());
    CHECK(afterFailure.value->content == beforeFailure.value->content);

    // Drafts live outside Content and are recoverable only against the same base.
    const auto formal = documents.snapshot(id);
    REQUIRE(formal.value.has_value());
    AutosaveService autosave(&store, "park-project");
    const auto      working = documents.content(id);
    REQUIRE(working.value.has_value());
    REQUIRE(autosave.writeDraft(*formal.value, *working.value).isAccepted());
    const auto draft = autosave.readDraft(id);
    REQUIRE(draft.isAccepted());
    CHECK(draft.value->content == versionValue(3));
    CHECK(autosave.shouldOfferRecovery(*draft.value, *formal.value));

    REQUIRE(documents.close(id).isAccepted());
    std::filesystem::remove_all(root, ec);
}
