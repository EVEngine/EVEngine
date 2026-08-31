#include "social_editing/SocialDocument.h"
#include "social/SocialGraph.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::social_editing;
using namespace eve::editing;

TEST_CASE("editor.social.graph_edges_are_reversible_validated_and_publishable") {
    SocialDocumentTarget document("diplomacy");
    for (const SocialEntityRecord& entity : std::vector<SocialEntityRecord>{
             {StableId("unit"), "Unit", "actor"},
             {StableId("player"), "Player", "faction"}}) {
        auto operation = document.makeSetEntity(entity); REQUIRE(operation.value);
        CHECK(document.applyDomainOperation(*operation.value).isAccepted());
    }
    SocialEdgeRecord owner{StableId("owns"), StableId("unit"), StableId("player"), "owner", {}, 1.0};
    auto set = document.makeSetEdge(owner); REQUIRE(set.value);
    CHECK(document.applyDomainOperation(*set.value).isAccepted());
    CHECK_EQ(static_cast<int>(document.makeDeleteEntity(StableId("unit")).status),
             static_cast<int>(EditorStatus::Conflict));
    eve::social::SocialGraph runtime;
    CHECK(SocialRuntimeApplier().apply(document, &runtime).isAccepted());
    CHECK_EQ(runtime.ownerOf("unit"), std::string("player"));

    DomainOperation undo = *set.value; undo.type = set.value->inverseType; undo.payload = set.value->inverse;
    CHECK(document.applyDomainOperation(undo).isAccepted());
    CHECK(document.edges().empty());
    CHECK(document.makeDeleteEntity(StableId("unit")).isAccepted());
}

TEST_CASE("editor.social.snapshot_rejects_dangling_or_duplicate_semantics_atomically") {
    SocialDocumentTarget source("source");
    CHECK(source.applyDomainOperation(*source.makeSetEntity({StableId("a"), "A", "actor"}).value).isAccepted());
    CHECK(source.applyDomainOperation(*source.makeSetEntity({StableId("b"), "B", "actor"}).value).isAccepted());
    CHECK(source.applyDomainOperation(*source.makeSetEdge({StableId("likes"), StableId("a"), StableId("b"), "relation", "likes", 0.8}).value).isAccepted());
    SocialDocumentTarget loaded("loaded");
    CHECK(loaded.loadSnapshot(source.snapshotValue()).isAccepted());
    CHECK_EQ(loaded.edges().size(), 1U);
    auto invalid = source.snapshotValue();
    auto* root = invalid.getIf<EditorValue::Object>(); REQUIRE(root);
    auto* entities = root->at("entities").getIf<EditorValue::Array>(); REQUIRE(entities);
    entities->clear();
    CHECK_EQ(static_cast<int>(loaded.loadSnapshot(invalid).status), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(loaded.entities().size(), 2U);
}
