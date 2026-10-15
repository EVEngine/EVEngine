#include "stylize/editor/EditorStylizeTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("authoringPreview.stylizeMissingProviderPreservesDocument") {
    eve::editor::StylizeRecipeTarget            document("recipe");
    const auto                                  revision = document.revision();
    const auto                                  before   = document.snapshotValue();
    eve::editor::StylizeOffscreenPreviewService previews(nullptr, nullptr);
    auto rejected = previews.render(document, nullptr, eve::editor::StableId("preview"), 32, 32);
    REQUIRE(!rejected.ok());
    CHECK(rejected.status().code() == eve::StatusCode::Rejected);
    CHECK(rejected.status().hasDiagnostics());
    CHECK(document.revision() == revision);
    CHECK(document.snapshotValue() == before);
}
