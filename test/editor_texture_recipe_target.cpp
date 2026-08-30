#include "editor/EditorTextureRecipeTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::editor;
namespace {
SelectionSnapshot select(const TextureRecipeTarget& target) {
    SelectionSnapshot selection;
    selection.channel = "texture-recipe";
    selection.items.push_back({SelectionDomain::Asset, TargetId(target.targetId()),
                               StableId(target.targetId()), "texture-recipe"});
    return selection;
}
void apply(TextureRecipeTarget& target, EditorResult<DomainOperation> operation) {
    REQUIRE(operation.value);
    REQUIRE(target.applyDomainOperation(*operation.value).isAccepted());
}
}
TEST_CASE("editor.texture_recipe.reflects_schema_and_reverses_parameters") {
    TextureRecipeTarget target("cloud.asset", "tex.cloud");
    const auto selection = select(target);
    const auto schema = target.schema(selection);
    REQUIRE(schema.find(PropertyPath("param.width")));
    REQUIRE(schema.find(PropertyPath("param.seamless")));
    auto width = target.makeSet(selection, PropertyPath("param.width"), int64_t{16},
                                PropertySetMode::Absolute);
    REQUIRE(width.value);
    REQUIRE(target.applyDomainOperation(*width.value).isAccepted());
    CHECK_EQ(*target.read(selection, PropertyPath("param.width")).value.getIf<int64_t>(), int64_t{16});
    DomainOperation undo = *width.value;
    undo.payload = width.value->inverse;
    REQUIRE(target.applyDomainOperation(undo).isAccepted());
    CHECK_NE(*target.read(selection, PropertyPath("param.width")).value.getIf<int64_t>(), int64_t{16});
    CHECK_EQ(static_cast<int>(target.makeSet(selection, PropertyPath("param.width"), int64_t{5000},
                                             PropertySetMode::Absolute).status),
             static_cast<int>(EditorStatus::Rejected));
}
TEST_CASE("editor.texture_recipe_snapshot_is_atomic_and_preview_is_deterministic") {
    TextureRecipeTarget target("cloud.asset", "tex.cloud");
    const auto selection = select(target);
    apply(target, target.makeSet(selection, PropertyPath("param.width"), int64_t{16}, PropertySetMode::Absolute));
    apply(target, target.makeSet(selection, PropertyPath("param.height"), int64_t{12}, PropertySetMode::Absolute));
    const auto before = target.snapshotValue();
    EditorValue invalid = before;
    auto* root = invalid.getIf<EditorValue::Object>();
    auto* content = (*root)["content"].getIf<EditorValue::Object>();
    auto* values = (*content)["values"].getIf<EditorValue::Object>();
    (*values)["width"] = int64_t{0};
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).status), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
    TextureRecipePreviewRuntime runtime;
    auto first = runtime.generate(target);
    REQUIRE(first.value);
    CHECK_EQ(first.value->width, 16);
    CHECK_EQ(first.value->height, 12);
    CHECK_NE(first.value->checksum, std::uint64_t{0});
    auto second = runtime.generate(target);
    REQUIRE(second.value);
    CHECK_EQ(second.value->checksum, first.value->checksum);
    CHECK_EQ(runtime.revision(), target.revision());
}
