#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "pixelworld/PixelMaterialCatalogCodec.h"
#include "pixelworld/PixelWorld.h"
#include "pixelworld_editor/PixelWorldCatalogPanel.h"
#include "ui/UIHost.h"

#include <string>

using namespace eve::pixelworld;
using namespace eve::pixelworld_editor;

namespace {

eve::ui::WidgetDesc* findWidget(eve::ui::WidgetDesc& root, const std::string& id) {
    if (root.id == id) return &root;
    for (auto& child : root.children)
        if (auto* found = findWidget(child, id)) return found;
    return nullptr;
}

}  // namespace

TEST_CASE("pixelworld_editor_material_draft_is_transactional_and_codec_canonical") {
    auto panel = PixelWorldCatalogPanel::builtIn();
    const auto initialRevision = panel.revision();
    const auto initialFingerprint = panel.draft().fingerprint();
    MaterialDefinition stone = panel.draft().definition(MaterialId::Stone);
    stone.displayRgba = 0x102030FFU;
    const auto receipt = panel.replaceMaterial(stone).expect("replace material draft");
    CHECK_EQ(receipt.revisionBefore, initialRevision);
    CHECK_EQ(receipt.revisionAfter, initialRevision + 1);
    CHECK(receipt.fingerprint != initialFingerprint);
    CHECK_EQ(panel.draft().definition(MaterialId::Stone).displayRgba,
             std::uint32_t(0x102030FFU));

    const auto beforeFailure = panel.document().expect("document before failure");
    stone.name = "water";
    CHECK(!panel.replaceMaterial(stone).ok());
    CHECK_EQ(panel.revision(), initialRevision + 1);
    CHECK_EQ(panel.document().expect("document after failure"), beforeFailure);

    auto decoded = PixelWorldCatalogPanel::create(beforeFailure).expect("decode panel document");
    CHECK_EQ(decoded.draft().fingerprint(), panel.draft().fingerprint());
    CHECK(!panel.loadDocument("{broken").ok());
    CHECK_EQ(panel.document().expect("document after malformed load"), beforeFailure);
}

TEST_CASE("pixelworld_editor_reaction_and_phase_operations_validate_whole_catalog") {
    auto panel = PixelWorldCatalogPanel::builtIn();
    MaterialReactionRule reaction;
    reaction.id = "editor-test-reaction";
    reaction.first = MaterialId::Water;
    reaction.second = MaterialId::Lava;
    reaction.firstResult = MaterialId::Steam;
    reaction.secondResult = MaterialId::Stone;
    reaction.heatDelta = 25;
    panel.addReaction(reaction).expect("add reaction");
    REQUIRE(panel.selectedReactionIndex().has_value());
    CHECK_EQ(panel.draft().reactions()[*panel.selectedReactionIndex()].id,
             std::string("editor-test-reaction"));

    auto duplicate = reaction;
    duplicate.first = MaterialId::Oil;
    CHECK(!panel.addReaction(std::move(duplicate)).ok());

    MaterialPhaseRule phase;
    phase.id = "editor-test-phase";
    phase.source = MaterialId::Water;
    phase.result = MaterialId::Steam;
    phase.threshold = 120;
    panel.addPhase(phase).expect("add phase");
    REQUIRE(panel.selectedPhaseIndex().has_value());
    CHECK_EQ(panel.draft().phaseRules()[*panel.selectedPhaseIndex()].id,
             std::string("editor-test-phase"));
    panel.removePhase(*panel.selectedPhaseIndex()).expect("remove phase");
}

TEST_CASE("pixelworld_editor_widget_tree_exposes_visual_material_and_rule_controls") {
    auto panel = PixelWorldCatalogPanel::builtIn();
    auto tree = panel.buildWidgetTree();
    CHECK_EQ(tree.id, std::string("pixelworld-catalog-editor"));
    REQUIRE(findWidget(tree, "catalog-browser") != nullptr);
    REQUIRE(findWidget(tree, "material-preview") != nullptr);
    REQUIRE(findWidget(tree, "reaction-section") != nullptr);
    REQUIRE(findWidget(tree, "phase-section") != nullptr);

    auto* stone = findWidget(tree, "material/1");
    REQUIRE(stone != nullptr);
    REQUIRE(bool(stone->onClick));
    stone->onClick();
    CHECK_EQ(panel.selectedMaterialIndex(), std::size_t(1));

    tree = panel.buildWidgetTree();
    auto* rgba = findWidget(tree, "material-rgba");
    REQUIRE(rgba != nullptr);
    REQUIRE(bool(rgba->onTextChange));
    rgba->onTextChange("#112233FF");
    CHECK_EQ(panel.draft().definition(MaterialId::Stone).displayRgba,
             std::uint32_t(0x112233FFU));
    CHECK(panel.statusText().find("validated") != std::string::npos);
}

TEST_CASE("pixelworld_editor_mounts_a_live_panel_and_reconciles_callback_edits") {
    auto panel = PixelWorldCatalogPanel::builtIn();
    const auto hostHandle = panel.open().expect("open catalog panel");
    REQUIRE(hostHandle);
    CHECK(panel.isOpen());
    auto host = eve::ui::UIHost::resolve(hostHandle);
    REQUIRE(host.has_value());
    auto stone = host->get().findById("material/1");
    REQUIRE(stone.has_value());
    const auto handler = stone->get().handlerClick;
    REQUIRE(handler > 0);
    auto click = host->get().tree()->clickHandlers[handler - 1];
    REQUIRE(bool(click));
    click();
    CHECK_EQ(panel.selectedMaterialIndex(), std::size_t(1));
    CHECK(host->get().findById("material-rgba").has_value());

    panel.close();
    CHECK(!panel.isOpen());
    panel.open().expect("reopen catalog panel");
    CHECK(panel.isOpen());
}

TEST_CASE("pixelworld_editor_apply_reuses_paused_authority_and_fingerprint_guard") {
    PixelWorld world(8601);
    auto& control = pixelWorldControlService();
    const auto worldId = world.worldLink().world;
    control.setPaused(worldId, true).expect("pause world");
    auto panel = PixelWorldCatalogPanel::builtIn();
    MaterialDefinition stone = panel.draft().definition(MaterialId::Stone);
    stone.displayRgba = 0xABCDEF88U;
    panel.replaceMaterial(std::move(stone)).expect("edit draft");
    const auto expected = world.materialCatalogFingerprint();
    panel.apply(control, worldId, expected).expect("apply Catalog panel");
    CHECK_EQ(world.materialDisplayRgba(MaterialId::Stone), std::uint32_t(0xABCDEF88U));
    CHECK(!panel.apply(control, worldId, expected).ok());
}
