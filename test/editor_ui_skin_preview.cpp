#include "ui_editor/EditorUiSkinPreview.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <map>

using namespace eve::editor;

namespace {
class SkinAssets final : public IUiSkinAssetResolver {
public:
    std::map<std::string, UiTextureMetadata> textures;
    std::map<std::string, bool> fonts;

    EditorResult<UiTextureMetadata> texture(const std::string& asset) const override {
        auto found = textures.find(asset);
        if (found == textures.end())
            return eve::editing::failed<UiTextureMetadata>(EditorStatus::NotFound,
                RuleId("test.ui.texture"), "missing texture");
        return eve::editing::applied<UiTextureMetadata>(found->second);
    }
    EditorResult<void> font(const std::string& asset) const override {
        if (!fonts.contains(asset))
            return eve::editing::failed<void>(EditorStatus::NotFound, RuleId("test.ui.font"),
                                             "missing font");
        return eve::editing::applied<void>();
    }
};
}

TEST_CASE("editor.ui.skin_content_is_reversible_and_snapshot_v2_round_trips") {
    UiDocumentTarget document("hud");
    UiLayoutValue layout; layout.width = 200.0; layout.height = 100.0;
    auto create = document.makeCreate({ObjectId("card"), {}, "image", "Card", layout});
    REQUIRE(create.ok()); REQUIRE(document.applyDomainOperation(create.value()).ok());
    UiContentValue content; content.fontAsset = "fonts/ui.ttf"; content.fontSize = 24.0;
    content.textureAsset = "images/card.png"; content.imageFit = "cover";
    content.horizontalAlign = "center"; content.verticalAlign = "end"; content.textR = 0.25;
    auto set = document.makeSetContent(ObjectId("card"), content);
    REQUIRE(set.ok()); REQUIRE(document.applyDomainOperation(set.value()).ok());
    CHECK_EQ(document.widget(ObjectId("card")).value().content.textureAsset,
             std::string("images/card.png"));
    REQUIRE(set.value().hasInverse);
    DomainOperation undo = set.value(); undo.payload = set.value().inverse;
    REQUIRE(document.applyDomainOperation(undo).ok());
    CHECK(document.widget(ObjectId("card")).value().content.textureAsset.empty());
    REQUIRE(document.applyDomainOperation(set.value()).ok());

    UiDocumentTarget restored("copy");
    REQUIRE(restored.loadSnapshot(document.snapshotValue()).ok());
    CHECK_EQ(restored.widget(ObjectId("card")).value().content.fontSize, 24.0);
}

TEST_CASE("editor.ui.skin_preview_resolves_assets_crops_cover_and_clips_to_parent") {
    UiDocumentTarget document("hud");
    UiLayoutValue rootLayout; rootLayout.width = 100.0; rootLayout.height = 80.0;
    auto root = document.makeCreate({ObjectId("root"), {}, "panel", "Root", rootLayout});
    REQUIRE(root.ok()); REQUIRE(document.applyDomainOperation(root.value()).ok());
    UiLayoutValue childLayout; childLayout.x = 80.0; childLayout.y = 10.0;
    childLayout.width = 60.0; childLayout.height = 40.0;
    auto child = document.makeCreate({ObjectId("hero"), ObjectId("root"), "image", "Hero", childLayout});
    REQUIRE(child.ok()); REQUIRE(document.applyDomainOperation(child.value()).ok());
    UiContentValue content; content.textureAsset = "hero.png"; content.imageFit = "cover";
    auto skin = document.makeSetContent(ObjectId("hero"), content);
    REQUIRE(skin.ok()); REQUIRE(document.applyDomainOperation(skin.value()).ok());

    SkinAssets assets; assets.textures["hero.png"] = {200.0, 100.0};
    UiDocumentPreviewService layouts;
    const auto preview = layouts.build(document, 100.0, 80.0);
    const auto plan = UiSkinPreviewPlanner().build(document, preview, assets);
    CHECK_EQ(static_cast<int>(plan.status), static_cast<int>(EditorStatus::Applied));
    REQUIRE_EQ(plan.commands.size(), static_cast<std::size_t>(1));
    CHECK(plan.commands[0].u0 > 0.0);
    CHECK_EQ(plan.commands[0].clipWidth, 20.0);

    SkinAssets missing;
    const auto rejected = UiSkinPreviewPlanner().build(document, preview, missing);
    CHECK_EQ(static_cast<int>(rejected.status), static_cast<int>(EditorStatus::Rejected));
    CHECK(!rejected.diagnostics.empty());
}
