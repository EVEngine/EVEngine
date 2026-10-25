// Related integration contracts share one translation unit to amortize header parsing.

// Contract section: editor material target
#include "PrivateTestScope.h"
#include "zeroerr/assert.h"

#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include <tuple>
#include "common/Module.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorPersistence.h"
#include "editor/EditorPluginPermissions.h"
#include "editor/EditorProjectSettings.h"
#include "editor/EditorTransactionService.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/material/editing/MaterialTarget.h"
#include "physics/editor/EditorPhysicsTarget.h"
#include "zeroerr/unittest.h"
// editor_material_target.cpp
namespace test_editor_material_target {
// editor_material_target.cpp
using namespace eve::material_editing;
using namespace eve::editing;
using eve::editor::LocalTransactionBackend;

namespace {

class NullMaterialAssets final : public IMaterialRuntimeAssetResolver {
public:
    Result<eve::graphics::Texture*> resolveTexture(const std::string&) const override {
        return eve::editing::applied<eve::graphics::Texture*>(&texture);
    }
    Result<eve::graphics::Shader*> resolveShader(const std::string&) const override {
        return eve::editing::applied<eve::graphics::Shader*>(nullptr);
    }

    mutable eve::graphics::Texture texture;
};

class SelectiveMaterialAssets final : public IMaterialRuntimeAssetResolver {
public:
    Result<eve::graphics::Texture*> resolveTexture(const std::string& asset) const override {
        if ((rejectDetail && asset == "asset://tve/detail-albedo") ||
            (rejectColors && asset == "asset://tve/colors-field") ||
            (rejectVertex && asset == "asset://tve/vertex-field") ||
            (rejectMotion && (asset == "asset://tve/motion-field" || asset == "asset://tve/motion-noise")))
            return eve::editing::failed<eve::graphics::Texture*>(EditorStatus::NotFound,
                                                                 RuleId("editor.material.test-missing-detail"),
                                                                 "Requested test texture is unavailable");
        return eve::editing::applied<eve::graphics::Texture*>(&texture);
    }
    Result<eve::graphics::Shader*> resolveShader(const std::string&) const override {
        return eve::editing::applied<eve::graphics::Shader*>(nullptr);
    }

    mutable eve::graphics::Texture texture;
    bool                           rejectDetail = true;
    bool                           rejectColors = false;
    bool                           rejectVertex = false;
    bool                           rejectMotion = false;
};

SelectionSnapshot materialSelection(const MaterialDocumentTarget& target) {
    SelectionSnapshot selection;
    selection.channel = "asset";
    SelectionItem item;
    item.domain = SelectionDomain::Asset;
    item.target = TargetId(target.targetId());
    item.item   = StableId(target.targetId().value());
    item.type   = "graphics.material";
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

Result<TransactionReceipt> commitMaterial(MaterialDocumentTarget& target, LocalTransactionBackend& transactions,
                                          const DomainOperation& operation, const char* transactionId) {
    TransactionSpec specification;
    specification.id           = TransactionId(transactionId);
    specification.label        = "Edit material";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    auto begun                 = transactions.begin(std::move(specification));
    if (!begun.ok())
        return eve::editing::failed<TransactionReceipt>(begun.code(), RuleId("test.material.begin"),
                                                        "Could not begin material transaction");
    auto appended = transactions.append(operation);
    if (!appended.ok()) {
        [[maybe_unused]] const auto rolledBack = transactions.rollback();
        return eve::editing::failed<TransactionReceipt>(appended.code(), RuleId("test.material.append"),
                                                        "Could not append material operation");
    }
    return transactions.commit();
}

void configureVegetationStages(MaterialDocumentTarget& target, const SelectionSnapshot& selection) {
    const auto set = [&](const char* path, EditorValue assigned) {
        auto operation = target.makeSet(selection, PropertyPath(path), assigned, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(target.applyDomainOperation(operation.value()).ok());
    };
    set("surface.mode", "masked");
    set("vegetation.alpha.enabled", true);
    set("vegetation.alpha.global", 0.7);
    set("vegetation.alpha.glancing", 0.3);
    set("vegetation.alpha.camera-min", 12.0);
    set("vegetation.alpha.camera-max", 80.0);
    set("vegetation.emission.enabled", true);
    set("vegetation.emission.minimum", 0.2);
    set("vegetation.emission.maximum", 0.8);
    set("vegetation.emission.phase", 0.6);
    set("vegetation.gradient.enabled", true);
    set("vegetation.gradient.color-one", EditorValue::Array{0.1, 0.2, 0.3, 1.0});
    set("vegetation.gradient.color-two", EditorValue::Array{0.7, 0.8, 0.9, 1.0});
    set("vegetation.gradient.minimum", 0.15);
    set("vegetation.gradient.maximum", 0.85);
    set("textures.orm", "asset://tve/leaf-orm");
    set("textures.emissive", "asset://tve/leaf-emissive");
    set("vegetation.translucency.enabled", true);
    set("vegetation.translucency.color", EditorValue::Array{0.4, 0.6, 0.2, 1.0});
    set("vegetation.translucency.intensity", 0.75);
    set("vegetation.translucency.strength", 3.0);
    set("vegetation.translucency.normal-distortion", 0.25);
    set("vegetation.translucency.scattering", 6.0);
    set("vegetation.translucency.direct", 0.8);
    set("vegetation.translucency.ambient", 0.2);
    set("vegetation.translucency.shadow", 0.65);
    set("vegetation.translucency.mask-amount", 0.5);
    set("vegetation.translucency.mask-minimum", 0.1);
    set("vegetation.translucency.mask-maximum", 0.9);
    set("vegetation.color.enabled", true);
    set("vegetation.color.field", EditorValue::Array{0.2, 0.3, 0.4, 0.6});
    set("vegetation.color.overlay-color", EditorValue::Array{0.8, 0.5, 0.2, 1.0});
    set("vegetation.color.vertex-occlusion-color", EditorValue::Array{0.4, 0.5, 0.6, 1.0});
    set("vegetation.color.overlay", 0.7);
    set("vegetation.color.wetness", 0.4);
    set("vegetation.color.colors-intensity", 1.5);
    set("vegetation.color.alpha-threshold-offset", -0.2);
    set("vegetation.color.invert-vertex-occlusion", true);
    set("vegetation.color.backface-normal", "same");
}

void checkPublishedVegetationStages(const eve::graphics::Material& runtime, const eve::graphics::PbrSurface& surface,
                                    eve::graphics::Texture* texture) {
    CHECK_EQ(runtime.surfaceMode(), eve::graphics::SurfaceMode::Masked);
    CHECK_EQ(surface.vegetationAlpha.global, 0.7f);
    CHECK_EQ(surface.vegetationAlpha.glancing, 0.3f);
    CHECK_EQ(surface.vegetationAlpha.cameraFadeMin, 12.f);
    CHECK_EQ(surface.vegetationAlpha.cameraFadeMax, 80.f);
    CHECK(surface.vegetationEmission.enabled);
    CHECK_EQ(surface.vegetationEmission.minimum, 0.2f);
    CHECK_EQ(surface.vegetationEmission.maximum, 0.8f);
    CHECK_EQ(surface.vegetationEmission.phase, 0.6f);
    CHECK(surface.vegetationGradient.enabled);
    CHECK_EQ(surface.vegetationGradient.colorOne[1], 0.2f);
    CHECK_EQ(surface.vegetationGradient.colorTwo[2], 0.9f);
    CHECK_EQ(surface.vegetationGradient.minimum, 0.15f);
    CHECK_EQ(surface.vegetationGradient.maximum, 0.85f);
    CHECK_EQ(surface.textures[std::size_t(eve::graphics::PbrTextureSlot::MetallicRoughness)].texture, texture);
    CHECK(!surface.textures[std::size_t(eve::graphics::PbrTextureSlot::MetallicRoughness)].srgbDecode);
    CHECK_EQ(surface.textures[std::size_t(eve::graphics::PbrTextureSlot::Emissive)].texture, texture);
    CHECK(surface.translucency.color[1] == 0.6f);
    CHECK_EQ(surface.translucency.intensity, 0.75f);
    CHECK_EQ(surface.translucency.strength, 3.f);
    CHECK_EQ(surface.translucency.scattering, 6.f);
    CHECK_EQ(surface.translucency.maskAmount, 0.5f);
    CHECK_EQ(surface.translucency.maskMinimum, 0.1f);
    CHECK_EQ(surface.translucency.maskMaximum, 0.9f);
    CHECK_EQ(surface.vegetationColor.fieldColor[3], 0.6f);
    CHECK_EQ(surface.vegetationColor.overlayColor[0], 0.8f);
    CHECK_EQ(surface.vegetationColor.vertexOcclusionColor[2], 0.6f);
    CHECK_EQ(surface.vegetationColor.overlay, 0.7f);
    CHECK_EQ(surface.vegetationColor.wetness, 0.4f);
    CHECK_EQ(surface.vegetationColor.colorsIntensity, 1.5f);
    CHECK_EQ(surface.vegetationColor.globalAlphaThresholdOffset, -0.2f);
    CHECK(surface.vegetationColor.invertVertexOcclusion);
    CHECK_EQ(surface.vegetationColor.backfaceNormalMode, eve::graphics::PbrVegetationBackfaceNormalMode::Same);
}

void configureVegetationDetail(MaterialDocumentTarget& target, const SelectionSnapshot& selection) {
    const auto set = [&](const char* path, EditorValue assigned) {
        auto operation = target.makeSet(selection, PropertyPath(path), assigned, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(target.applyDomainOperation(operation.value()).ok());
    };
    set("vegetation.detail.enabled", true);
    set("vegetation.detail.albedo", "asset://tve/detail-albedo");
    set("vegetation.detail.normal", "asset://tve/detail-normal");
    set("vegetation.detail.mask", "asset://tve/detail-mask");
    set("vegetation.detail.uv-mode", "world");
    set("vegetation.detail.uv-scale", EditorValue::Array{2.0, 3.0});
    set("vegetation.detail.uv-offset", EditorValue::Array{0.25, 0.5});
    set("vegetation.detail.inverse-uv-scale", true);
    set("vegetation.detail.color", EditorValue::Array{0.1, 0.2, 0.3, 0.4});
    set("vegetation.detail.color-two", EditorValue::Array{0.6, 0.7, 0.8, 0.9});
    set("vegetation.detail.color-mode", "variation");
    set("vegetation.detail.blend-mode", "replace");
    set("vegetation.detail.alpha-mode", "main");
    set("vegetation.detail.value", 0.75);
    set("vegetation.detail.normal-value", 2.0);
    set("vegetation.detail.normal-blend", 0.65);
    set("vegetation.detail.albedo-value", 0.55);
    set("vegetation.detail.metallic", 0.35);
    set("vegetation.detail.occlusion", 0.45);
    set("vegetation.detail.smoothness", 0.85);
    set("vegetation.detail.blend-minimum", 0.15);
    set("vegetation.detail.blend-maximum", 0.8);
    set("vegetation.detail.mask-mode", "inverse");
    set("vegetation.detail.mask-minimum", 0.2);
    set("vegetation.detail.mask-maximum", 0.7);
    set("vegetation.detail.mesh-mode", "inverse");
    set("vegetation.detail.mesh-minimum", 0.3);
    set("vegetation.detail.mesh-maximum", 0.9);
}

void checkVegetationDetail(const eve::graphics::PbrSurface& surface, eve::graphics::Texture* texture) {
    CHECK_EQ(surface.vegetationDetail.textures[0].texture, texture);
    CHECK_EQ(surface.vegetationDetail.textures[1].texture, texture);
    CHECK_EQ(surface.vegetationDetail.textures[2].texture, texture);
    CHECK(surface.vegetationDetail.textures[0].srgbDecode);
    CHECK(!surface.vegetationDetail.textures[1].srgbDecode);
    CHECK(!surface.vegetationDetail.textures[2].srgbDecode);
    CHECK_EQ(surface.vegetationDetail.uvMode, 2u);
    CHECK_EQ(surface.vegetationDetail.colorMode, 1u);
    CHECK_EQ(surface.vegetationDetail.blendMode, 1u);
    CHECK_EQ(surface.vegetationDetail.alphaMode, 0u);
    CHECK_EQ(surface.vegetationDetail.uvScale[1], 3.f);
    CHECK_EQ(surface.vegetationDetail.uvOffset[0], 0.25f);
    CHECK(surface.vegetationDetail.inverseUvScale);
    CHECK_EQ(surface.vegetationDetail.color[2], 0.3f);
    CHECK_EQ(surface.vegetationDetail.colorTwo[3], 0.9f);
    CHECK_EQ(surface.vegetationDetail.value, 0.75f);
    CHECK_EQ(surface.vegetationDetail.normalValue, 2.f);
    CHECK_EQ(surface.vegetationDetail.normalBlendValue, 0.65f);
    CHECK_EQ(surface.vegetationDetail.albedoValue, 0.55f);
    CHECK_EQ(surface.vegetationDetail.metallicValue, 0.35f);
    CHECK_EQ(surface.vegetationDetail.occlusionValue, 0.45f);
    CHECK_EQ(surface.vegetationDetail.smoothnessValue, 0.85f);
    CHECK_EQ(surface.vegetationDetail.blendMinimum, 0.15f);
    CHECK_EQ(surface.vegetationDetail.blendMaximum, 0.8f);
    CHECK_EQ(surface.vegetationDetail.maskMode, 1u);
    CHECK_EQ(surface.vegetationDetail.maskMinimum, 0.2f);
    CHECK_EQ(surface.vegetationDetail.maskMaximum, 0.7f);
    CHECK_EQ(surface.vegetationDetail.meshMode, 1u);
    CHECK_EQ(surface.vegetationDetail.meshMinimum, 0.3f);
    CHECK_EQ(surface.vegetationDetail.meshMaximum, 0.9f);
}

void configureVegetationFields(MaterialDocumentTarget& target, const SelectionSnapshot& selection) {
    const auto set = [&](const char* path, EditorValue assigned) {
        auto operation = target.makeSet(selection, PropertyPath(path), assigned, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(target.applyDomainOperation(operation.value()).ok());
    };
    for (const char* group : {"extras", "colors"}) {
        const std::string prefix = std::string("vegetation.") + group;
        set((prefix + ".enabled").c_str(), true);
        set((prefix + ".texture").c_str(), std::string("asset://tve/") + group + "-field");
        set((prefix + ".layer").c_str(), std::int64_t(group == std::string("extras") ? 3 : 7));
        set((prefix + ".coords").c_str(), EditorValue::Array{2.0, 4.0, 0.25, 0.5});
        set((prefix + ".use-pivot-position").c_str(), true);
        for (std::size_t i = 0; i < 9; ++i) set((prefix + ".usage-" + std::to_string(i)).c_str(), double(i + 1) / 10.0);
    }
    set("vegetation.extras.fallback", EditorValue::Array{0.1, 0.2, 0.3, 0.4});
    set("vegetation.colors.fallback", EditorValue::Array{0.5, 0.6, 0.7, 0.8});
}

template <class Field>
void checkVegetationField(const Field& field, eve::graphics::Texture* texture, uint32_t layer, float fallbackFirst) {
    CHECK_EQ(field.texture, texture);
    CHECK_EQ(field.layer, layer);
    CHECK(field.usePivotPosition);
    CHECK_EQ(field.coords[1], 4.f);
    CHECK_EQ(field.coords[3], 0.5f);
    CHECK_EQ(field.fallback[0], fallbackFirst);
    CHECK_EQ(field.usage[0], 0.1f);
    CHECK_EQ(field.usage[8], 0.9f);
}

void configureVegetationVertex(MaterialDocumentTarget& target, const SelectionSnapshot& selection) {
    const auto set = [&](const char* path, EditorValue assigned) {
        auto operation = target.makeSet(selection, PropertyPath(path), assigned, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(target.applyDomainOperation(operation.value()).ok());
    };
    set("vegetation.vertex.enabled", true);
    set("vegetation.vertex.texture", "asset://tve/vertex-field");
    set("vegetation.vertex.layer", std::int64_t(6));
    set("vegetation.vertex.fallback", EditorValue::Array{0.1, 0.2, 0.3, 0.8});
    set("vegetation.vertex.coords", EditorValue::Array{3.0, 4.0, 0.25, 0.75});
    for (std::size_t i = 0; i < 9; ++i)
        set(("vegetation.vertex.usage-" + std::to_string(i)).c_str(), double(i + 1) / 10.0);
    set("vegetation.vertex.source", "gpu-fields");
    set("vegetation.vertex.global-size", 0.85);
    set("vegetation.vertex.size-fade-start", 12.0);
    set("vegetation.vertex.size-fade-end", 96.0);
    set("vegetation.vertex.distance-fade-bias", 1.5);
}

void configureVegetationMotion(MaterialDocumentTarget& target, const SelectionSnapshot& selection) {
    const auto set = [&](const char* path, EditorValue assigned) {
        auto operation = target.makeSet(selection, PropertyPath(path), assigned, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(target.applyDomainOperation(operation.value()).ok());
    };
    set("vegetation.motion.mode", "object");
    set("vegetation.motion.texture", "asset://tve/motion-field");
    set("vegetation.motion.noise", "asset://tve/motion-noise");
    set("vegetation.motion.layer", std::int64_t(5));
    set("vegetation.motion.fallback", EditorValue::Array{0.1, 0.2, 0.3, 0.4});
    set("vegetation.motion.coords", EditorValue::Array{2.0, 3.0, 0.25, 0.75});
    set("vegetation.motion.global-direction", EditorValue::Array{0.6, 0.8});
    set("vegetation.motion.world-origin", EditorValue::Array{10.0, 20.0, 30.0});
    for (std::size_t i = 0; i < 9; ++i)
        set(("vegetation.motion.usage-" + std::to_string(i)).c_str(), double(i + 1) / 10.0);
    set("vegetation.motion.time", 42.5);
    set("vegetation.motion.dynamic-mode", 0.1);
    set("vegetation.motion.rigidity", 0.2);
    set("vegetation.motion.facing", 0.3);
    set("vegetation.motion.interaction-mask", 0.4);
    const std::array<const char*, 22> names = {
        "bending",           "bending-speed",    "bending-scale",     "bending-variation", "branch",
        "rolling",           "branch-speed",     "branch-scale",      "branch-variation",  "flutter",
        "flutter-speed",     "flutter-scale",    "flutter-variation", "global-bending",    "global-branch",
        "global-flutter",    "noise-tiling",     "interaction",       "fade-distance",     "perspective-push",
        "perspective-noise", "perspective-angle"};
    for (std::size_t i = 0; i < names.size(); ++i)
        set((std::string("vegetation.motion.") + names[i]).c_str(), double(i + 1));
}

}  // namespace

TEST_CASE("editor.material.schema_matches_core_material_authoring_surface") {
    MaterialDocumentTarget  target("metal-panel");
    const SelectionSnapshot selection = materialSelection(target);
    const PropertySchema    schema    = target.schema(selection);
    CHECK_EQ(schema.typeId, std::string("graphics.material"));
    CHECK(schema.find(PropertyPath("shading.metallic")) != nullptr);
    CHECK(schema.find(PropertyPath("shading.roughness")) != nullptr);
    CHECK(schema.find(PropertyPath("textures.normal")) != nullptr);
    CHECK(schema.find(PropertyPath("surface.blend")) != nullptr);
    CHECK(schema.find(PropertyPath("shadow.receive")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.alpha.glancing")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.alpha.noise")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.emission.phase")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.gradient.color-two")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.translucency.scattering")) != nullptr);
    CHECK(schema.find(PropertyPath("textures.orm")) != nullptr);
    CHECK(schema.find(PropertyPath("textures.emissive")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.color.overlay-subsurface")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.color.backface-normal")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.detail.normal-blend")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.detail.uv-mode")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.extras.usage-8")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.colors.use-pivot-position")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.vertex.distance-fade-bias")) != nullptr);
    CHECK(schema.find(PropertyPath("vegetation.motion.perspective-angle")) != nullptr);
    CHECK(target.read(selection, PropertyPath("vegetation.detail.albedo")).value.getIf<std::string>() != nullptr);
    CHECK(target.queryCapability(CapabilityId("eve.editor.target.material-properties")) != nullptr);
}

TEST_CASE("editor.material.vegetation_alpha_properties_are_reversible_and_persistent") {
    MaterialDocumentTarget  target("leaf-alpha");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    const SelectionSnapshot selection = materialSelection(target);

    for (const auto& [path, value, id] :
         {std::tuple{PropertyPath("vegetation.alpha.enabled"), EditorValue(true), "vegetation.alpha.enabled"},
          {PropertyPath("vegetation.alpha.glancing"), EditorValue(0.35), "vegetation.alpha.glancing"},
          {PropertyPath("vegetation.alpha.camera-min"), EditorValue(8.0), "vegetation.alpha.camera-min"},
          {PropertyPath("vegetation.alpha.camera-max"), EditorValue(64.0), "vegetation.alpha.camera-max"},
          {PropertyPath("vegetation.alpha.noise"), EditorValue("asset://tve/noise-volume"),
           "vegetation.alpha.noise"}}) {
        auto operation = target.makeSet(selection, path, value, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(commitMaterial(target, transactions, operation.value(), id).ok());
    }
    CHECK(target.validate().empty());
    REQUIRE(transactions.undo().ok());
    CHECK(target.read(selection, PropertyPath("vegetation.alpha.noise")).value == EditorValue(""));
    REQUIRE(transactions.redo().ok());

    MaterialDocumentTarget restored("restored-alpha");
    REQUIRE(restored.loadSnapshot(target.snapshotValue()).ok());
    const SelectionSnapshot restoredSelection = materialSelection(restored);
    CHECK(restored.read(restoredSelection, PropertyPath("vegetation.alpha.glancing")).value == EditorValue(0.35));
    CHECK(restored.read(restoredSelection, PropertyPath("vegetation.alpha.noise")).value ==
          EditorValue("asset://tve/noise-volume"));

    auto invalid = target.makeSet(selection, PropertyPath("vegetation.alpha.constant"), 1.1, PropertySetMode::Absolute);
    CHECK_EQ(static_cast<int>(invalid.code()), static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.material.legacy_snapshots_migrate_with_vegetation_defaults") {
    for (std::int64_t version : {1, 2, 3, 4, 5, 6, 7, 8}) {
        EditorValue::Object properties;
        properties["shading.metallic"] = 0.2;
        EditorValue::Object snapshot;
        snapshot["schemaVersion"] = version;
        snapshot["properties"]    = std::move(properties);

        MaterialDocumentTarget target("legacy-material");
        REQUIRE(target.loadSnapshot(EditorValue(std::move(snapshot))).ok());
        const SelectionSnapshot selection = materialSelection(target);
        CHECK(target.read(selection, PropertyPath("shading.metallic")).value == EditorValue(0.2));
        CHECK(target.read(selection, PropertyPath("vegetation.alpha.enabled")).value == EditorValue(false));
        CHECK(target.read(selection, PropertyPath("vegetation.emission.enabled")).value == EditorValue(false));
        CHECK(target.read(selection, PropertyPath("vegetation.gradient.enabled")).value == EditorValue(false));
        CHECK(target.read(selection, PropertyPath("vegetation.translucency.enabled")).value == EditorValue(false));
        CHECK(target.read(selection, PropertyPath("vegetation.color.enabled")).value == EditorValue(false));
        CHECK(target.read(selection, PropertyPath("vegetation.detail.enabled")).value == EditorValue(false));
        CHECK(target.read(selection, PropertyPath("vegetation.extras.enabled")).value == EditorValue(false));
        CHECK(target.read(selection, PropertyPath("vegetation.colors.enabled")).value == EditorValue(false));
        CHECK(target.read(selection, PropertyPath("vegetation.vertex.enabled")).value == EditorValue(false));
        CHECK(target.read(selection, PropertyPath("vegetation.motion.mode")).value == EditorValue("disabled"));
        const EditorValue encodedSnapshot = target.snapshotValue();
        const auto*       encoded         = encodedSnapshot.getIf<EditorValue::Object>();
        REQUIRE(encoded != nullptr);
        CHECK(encoded->at("schemaVersion") == EditorValue(9));
    }
}

TEST_CASE("editor.material.translucency_mask_requires_orm_atomically") {
    MaterialDocumentTarget  source("masked-translucency");
    const SelectionSnapshot selection = materialSelection(source);
    for (const auto& [path, value] : {std::pair{PropertyPath("vegetation.translucency.enabled"), EditorValue(true)},
                                      {PropertyPath("vegetation.translucency.intensity"), EditorValue(0.8)},
                                      {PropertyPath("vegetation.translucency.mask-amount"), EditorValue(0.5)}}) {
        auto operation = source.makeSet(selection, path, value, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(source.applyDomainOperation(operation.value()).ok());
    }

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    NullMaterialAssets              assets;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget        target("masked-translucency", &sink);
    const Revision                  before = target.revision();
    CHECK_EQ(static_cast<int>(target.reloadSnapshot(source.snapshotValue()).code()),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.revision(), before);
    CHECK(!runtime.hasPbrSurface());
}

TEST_CASE("editor.material.vegetation_stages_snapshot_reaches_bound_runtime_material") {
    MaterialDocumentTarget  source("runtime-leaf");
    const SelectionSnapshot selection = materialSelection(source);
    configureVegetationStages(source, selection);

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    NullMaterialAssets              assets;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget        target("runtime-leaf", &sink);
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    REQUIRE(runtime.hasPbrSurface());
    const auto publishedSurface = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    checkPublishedVegetationStages(runtime, *publishedSurface, &assets.texture);

    MaterialDocumentTarget disabled("runtime-leaf");
    REQUIRE(target.reloadSnapshot(disabled.snapshotValue()).ok());
    const auto clearedSurface = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    CHECK(!clearedSurface->vegetationAlpha.enabled);
    CHECK(!clearedSurface->vegetationEmission.enabled);
    CHECK(!clearedSurface->vegetationGradient.enabled);
    CHECK_EQ(clearedSurface->translucency.intensity, 0.f);
    CHECK_EQ(clearedSurface->translucency.maskAmount, 0.f);
    CHECK_EQ(clearedSurface->vegetationColor.overlay, 0.f);
    CHECK_EQ(clearedSurface->vegetationColor.wetness, 0.f);
    CHECK(!clearedSurface->vegetationColor.invertVertexOcclusion);
    CHECK_EQ(runtime.surfaceMode(), eve::graphics::SurfaceMode::Opaque);

    auto* unbound = eve::graphics::Renderable3D::create();
    REQUIRE(unbound != nullptr);
    Renderable3DMaterialRuntimeSink unboundSink(unbound, &assets);
    MaterialPublishingTarget        unboundTarget("runtime-leaf", &unboundSink);
    const auto                      before = unboundTarget.revision();
    CHECK_EQ(static_cast<int>(unboundTarget.reloadSnapshot(source.snapshotValue()).code()),
             static_cast<int>(EditorStatus::Unsupported));
    CHECK_EQ(unboundTarget.revision(), before);

    for (const auto& [path, value] : {std::pair{PropertyPath("surface.mode"), EditorValue("opaque")},
                                      {PropertyPath("vegetation.alpha.enabled"), EditorValue(false)}}) {
        auto operation = source.makeSet(selection, path, value, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(source.applyDomainOperation(operation.value()).ok());
    }
    CHECK_EQ(static_cast<int>(unboundTarget.reloadSnapshot(source.snapshotValue()).code()),
             static_cast<int>(EditorStatus::Unsupported));
    CHECK_EQ(unboundTarget.revision(), before);
}

TEST_CASE("editor.material.missing_vegetation_detail_texture_rejects_atomically") {
    MaterialDocumentTarget  source("missing-runtime-detail");
    const SelectionSnapshot selection = materialSelection(source);
    configureVegetationDetail(source, selection);

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    SelectiveMaterialAssets         assets;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget        target("missing-runtime-detail", &sink);
    const Revision                  before = target.revision();

    CHECK_EQ(static_cast<int>(target.reloadSnapshot(source.snapshotValue()).code()),
             static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(target.revision(), before);
    CHECK(!runtime.hasPbrSurface());
}

TEST_CASE("editor.material.vegetation_detail_snapshot_reaches_bound_runtime_material") {
    MaterialDocumentTarget  source("runtime-detail");
    const SelectionSnapshot selection = materialSelection(source);
    configureVegetationDetail(source, selection);

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    NullMaterialAssets              assets;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget        target("runtime-detail", &sink);
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    REQUIRE(runtime.hasPbrSurface());
    const auto published = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    checkVegetationDetail(*published, &assets.texture);

    MaterialDocumentTarget disabled("runtime-detail");
    REQUIRE(target.reloadSnapshot(disabled.snapshotValue()).ok());
    const auto cleared = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    CHECK_EQ(cleared->vegetationDetail.value, 0.f);
    CHECK_EQ(cleared->vegetationDetail.textures[0].texture, nullptr);
}

TEST_CASE("editor.material.vegetation_field_sampling_snapshot_reaches_bound_runtime_material") {
    MaterialDocumentTarget  source("runtime-fields");
    const SelectionSnapshot selection = materialSelection(source);
    configureVegetationFields(source, selection);

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    SelectiveMaterialAssets assets;
    assets.rejectDetail = false;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget        target("runtime-fields", &sink);
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    REQUIRE(runtime.hasPbrSurface());
    const auto published = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    checkVegetationField(published->vegetationExtras, &assets.texture, 3u, 0.1f);
    checkVegetationField(published->vegetationColors, &assets.texture, 7u, 0.5f);

    for (const char* path : {"vegetation.extras.enabled", "vegetation.colors.enabled"}) {
        auto operation = source.makeSet(selection, PropertyPath(path), false, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(source.applyDomainOperation(operation.value()).ok());
    }
    assets.rejectColors = true;
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    const auto cleared = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    CHECK_EQ(cleared->vegetationExtras.texture, nullptr);
    CHECK_EQ(cleared->vegetationColors.texture, nullptr);
    CHECK_EQ(cleared->vegetationColors.fallback[3], 0.f);
}

TEST_CASE("editor.material.vegetation_vertex_snapshot_reaches_bound_runtime_material") {
    MaterialDocumentTarget  source("runtime-vertex");
    const SelectionSnapshot selection = materialSelection(source);
    configureVegetationVertex(source, selection);

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    SelectiveMaterialAssets assets;
    assets.rejectDetail = false;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget        target("runtime-vertex", &sink);
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    REQUIRE(runtime.hasPbrSurface());
    const auto  published = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    const auto& vertex    = published->vegetationVertex;
    CHECK_EQ(vertex.texture, &assets.texture);
    CHECK_EQ(vertex.layer, 6u);
    CHECK_EQ(vertex.fallback[3], 0.8f);
    CHECK_EQ(vertex.coords[2], 0.25f);
    CHECK_EQ(vertex.usage[8], 0.9f);
    CHECK_EQ(vertex.source, eve::graphics::PbrVegetationDeformationSource::GpuFields);
    CHECK_EQ(vertex.globalSize, 0.85f);
    CHECK_EQ(vertex.sizeFadeStart, 12.f);
    CHECK_EQ(vertex.sizeFadeEnd, 96.f);
    CHECK_EQ(vertex.distanceFadeBias, 1.5f);

    auto disable =
        source.makeSet(selection, PropertyPath("vegetation.vertex.enabled"), false, PropertySetMode::Absolute);
    REQUIRE(disable.ok());
    REQUIRE(source.applyDomainOperation(disable.value()).ok());
    assets.rejectVertex = true;
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    const auto cleared = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    CHECK_EQ(cleared->vegetationVertex.texture, nullptr);
    CHECK_EQ(cleared->vegetationVertex.source, eve::graphics::PbrVegetationDeformationSource::RestMesh);
}

TEST_CASE("editor.material.missing_vegetation_vertex_texture_rejects_atomically") {
    MaterialDocumentTarget  source("missing-runtime-vertex");
    const SelectionSnapshot selection = materialSelection(source);
    configureVegetationVertex(source, selection);

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    SelectiveMaterialAssets assets;
    assets.rejectDetail = false;
    assets.rejectVertex = true;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget        target("missing-runtime-vertex", &sink);
    const Revision                  before = target.revision();

    CHECK_EQ(static_cast<int>(target.reloadSnapshot(source.snapshotValue()).code()),
             static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(target.revision(), before);
    CHECK(!runtime.hasPbrSurface());
}

TEST_CASE("editor.material.vegetation_motion_snapshot_reaches_bound_runtime_material") {
    MaterialDocumentTarget  source("runtime-motion");
    const SelectionSnapshot selection = materialSelection(source);
    configureVegetationVertex(source, selection);
    configureVegetationMotion(source, selection);

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    SelectiveMaterialAssets assets;
    assets.rejectDetail = false;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget        target("runtime-motion", &sink);
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    REQUIRE(runtime.hasPbrSurface());
    const auto  published = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    const auto& motion    = published->vegetationMotion;
    CHECK_EQ(motion.texture, &assets.texture);
    CHECK_EQ(motion.noise, &assets.texture);
    CHECK_EQ(motion.mode, eve::graphics::PbrVegetationMotionMode::Object);
    CHECK_EQ(motion.layer, 5u);
    CHECK_EQ(motion.fallback[2], 0.3f);
    CHECK_EQ(motion.coords[3], 0.75f);
    CHECK_EQ(motion.globalDirection[1], 0.8f);
    CHECK_EQ(motion.worldOrigin[2], 30.f);
    CHECK_EQ(motion.usage[8], 0.9f);
    CHECK_EQ(motion.time, 42.5);
    CHECK_EQ(motion.dynamicMode, 0.1f);
    CHECK_EQ(motion.rigidity, 0.2f);
    CHECK_EQ(motion.facing, 0.3f);
    CHECK_EQ(motion.interactionMask, 0.4f);
    CHECK_EQ(motion.bending, 1.f);
    CHECK_EQ(motion.bendingSpeed, 2.f);
    CHECK_EQ(motion.bendingScale, 3.f);
    CHECK_EQ(motion.bendingVariation, 4.f);
    CHECK_EQ(motion.branch, 5.f);
    CHECK_EQ(motion.rolling, 6.f);
    CHECK_EQ(motion.branchSpeed, 7.f);
    CHECK_EQ(motion.branchScale, 8.f);
    CHECK_EQ(motion.branchVariation, 9.f);
    CHECK_EQ(motion.flutter, 10.f);
    CHECK_EQ(motion.flutterSpeed, 11.f);
    CHECK_EQ(motion.flutterScale, 12.f);
    CHECK_EQ(motion.flutterVariation, 13.f);
    CHECK_EQ(motion.globalBending, 14.f);
    CHECK_EQ(motion.globalBranch, 15.f);
    CHECK_EQ(motion.globalFlutter, 16.f);
    CHECK_EQ(motion.noiseTiling, 17.f);
    CHECK_EQ(motion.interaction, 18.f);
    CHECK_EQ(motion.fadeDistance, 19.f);
    CHECK_EQ(motion.perspectivePush, 20.f);
    CHECK_EQ(motion.perspectiveNoise, 21.f);
    CHECK_EQ(motion.perspectiveAngle, 22.f);

    auto disable =
        source.makeSet(selection, PropertyPath("vegetation.motion.mode"), "disabled", PropertySetMode::Absolute);
    REQUIRE(disable.ok());
    REQUIRE(source.applyDomainOperation(disable.value()).ok());
    assets.rejectMotion = true;
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    const auto cleared = std::make_unique<eve::graphics::PbrSurface>(runtime.pbrSurface());
    CHECK_EQ(cleared->vegetationMotion.texture, nullptr);
    CHECK_EQ(cleared->vegetationMotion.noise, nullptr);
    CHECK_EQ(cleared->vegetationMotion.mode, eve::graphics::PbrVegetationMotionMode::Disabled);
}

TEST_CASE("editor.material.missing_vegetation_motion_texture_rejects_atomically") {
    MaterialDocumentTarget  source("missing-runtime-motion");
    const SelectionSnapshot selection = materialSelection(source);
    configureVegetationVertex(source, selection);
    configureVegetationMotion(source, selection);

    auto* renderable = eve::graphics::Renderable3D::create();
    REQUIRE(renderable != nullptr);
    eve::graphics::Material runtime;
    renderable->setMaterial(&runtime);
    SelectiveMaterialAssets assets;
    assets.rejectDetail = false;
    assets.rejectMotion = true;
    Renderable3DMaterialRuntimeSink sink(renderable, &assets);
    MaterialPublishingTarget        target("missing-runtime-motion", &sink);
    const Revision                  before = target.revision();

    CHECK_EQ(static_cast<int>(target.reloadSnapshot(source.snapshotValue()).code()),
             static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(target.revision(), before);
    CHECK(!runtime.hasPbrSurface());
}

TEST_CASE("editor.material.properties_are_validated_and_reversible") {
    MaterialDocumentTarget  target("metal-panel");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    const SelectionSnapshot selection = materialSelection(target);

    auto invalid = target.makeSet(selection, PropertyPath("shading.metallic"), 2.0, PropertySetMode::Absolute);
    CHECK_EQ(static_cast<int>(invalid.code()), static_cast<int>(EditorStatus::Rejected));

    auto change = target.makeSet(selection, PropertyPath("shading.metallic"), 0.8, PropertySetMode::Absolute);
    REQUIRE(change.ok());
    REQUIRE(commitMaterial(target, transactions, change.value(), "material.metallic").ok());
    CHECK(target.read(selection, PropertyPath("shading.metallic")).value == EditorValue(0.8));
    REQUIRE(transactions.undo().ok());
    CHECK(target.read(selection, PropertyPath("shading.metallic")).value == EditorValue(0.0));
    REQUIRE(transactions.redo().ok());
    CHECK(target.read(selection, PropertyPath("shading.metallic")).value == EditorValue(0.8));
}

TEST_CASE("editor.material.snapshot_roundtrip_and_cross_field_diagnostics") {
    MaterialDocumentTarget  source("source");
    LocalWorldAuthority     authority(&source);
    LocalTransactionBackend transactions(&authority);
    const SelectionSnapshot selection = materialSelection(source);

    for (const auto& [path, value, id] :
         {std::tuple{PropertyPath("shading.model"), EditorValue("custom"), "material.model"},
          {PropertyPath("surface.mode"), EditorValue("masked"), "material.surface"},
          {PropertyPath("textures.height"), EditorValue("asset://height.png"), "material.height"}}) {
        auto operation = source.makeSet(selection, path, value, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(commitMaterial(source, transactions, operation.value(), id).ok());
    }
    CHECK_EQ(source.validate().size(), static_cast<std::size_t>(3));

    MaterialDocumentTarget restored("restored");
    REQUIRE(restored.loadSnapshot(source.snapshotValue()).ok());
    const SelectionSnapshot restoredSelection = materialSelection(restored);
    CHECK(restored.read(restoredSelection, PropertyPath("shading.model")).value == EditorValue("custom"));
    CHECK(restored.read(restoredSelection, PropertyPath("textures.height")).value == EditorValue("asset://height.png"));

    EditorValue::Object invalidRoot;
    invalidRoot["schemaVersion"] = 99;
    invalidRoot["properties"]    = EditorValue::Object{};
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(EditorValue(std::move(invalidRoot))).code()),
             static_cast<int>(EditorStatus::Rejected));
}

// editor_persistence_v2.cpp
using namespace eve::editor;

TEST_CASE("editor.v2.scene_document_and_savegame_share_one_persistence_contract") {
    MemoryAtomicDocumentStore documentStore;
    MemoryAtomicDocumentStore saveGameStore;
    EditorPersistenceAdapter  document(EditorPersistenceKind::SceneDocument, &documentStore,
                                       "content://World/Park.evscene");
    EditorPersistenceAdapter  saveGame(EditorPersistenceKind::SaveGame, &saveGameStore, "savegame://player-1/park");
    auto                      documentBase = document.load();
    auto                      saveBase     = saveGame.load();
    REQUIRE(documentBase.ok());
    REQUIRE(saveBase.ok());

    DomainOperation operation;
    operation.type        = "park.place-tree";
    operation.inverseType = "park.remove-tree";
    operation.target      = TargetId("park-world");
    operation.payload     = EditorValue::Object{{"species", EditorValue("oak")}};
    operation.hasInverse  = true;
    const EditorValue checkpoint(EditorValue::Object{{"treeCount", EditorValue(1)}});
    auto              documentCommit = document.commit(documentBase.value(), checkpoint, {operation});
    auto              saveCommit     = saveGame.commit(saveBase.value(), checkpoint, {operation});
    REQUIRE(documentCommit.ok());
    REQUIRE(saveCommit.ok());
    CHECK(documentCommit.value().content == saveCommit.value().content);
    CHECK_EQ(documentCommit.value().journal.size(), saveCommit.value().journal.size());
    CHECK_EQ(documentCommit.value().journal.front().type, saveCommit.value().journal.front().type);

    auto stale = saveGame.commit(saveBase.value(), checkpoint, {operation});
    CHECK_EQ(static_cast<int>(stale.code()), static_cast<int>(EditorStatus::Conflict));
}

// editor_physics_target.cpp
using namespace eve::editor;

namespace {

SelectionSnapshot colliderSelection(const PhysicsColliderTarget& target) {
    SelectionSnapshot selection;
    selection.channel = "scene";
    SelectionItem item;
    item.domain = SelectionDomain::Scene;
    item.target = TargetId(target.targetId());
    item.item   = StableId(target.targetId().value());
    item.type   = target.describe().type;
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

SelectionSnapshot jointSelection(const PhysicsJointTarget& target) {
    SelectionSnapshot selection;
    selection.channel = "scene";
    SelectionItem item;
    item.domain = SelectionDomain::Scene;
    item.target = TargetId(target.targetId());
    item.item   = StableId(target.targetId().value());
    item.type   = target.describe().type;
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

Result<TransactionReceipt> commitCollider(PhysicsColliderTarget& target, LocalTransactionBackend& transactions,
                                          const DomainOperation& operation, const char* id) {
    TransactionSpec specification;
    specification.id           = TransactionId(id);
    specification.label        = "Edit collider";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    auto begun                 = transactions.begin(std::move(specification));
    if (!begun.ok())
        return eve::editing::failed<TransactionReceipt>(begun.code(), RuleId("test.physics.begin"),
                                                        "Could not begin collider transaction");
    auto appended = transactions.append(operation);
    if (!appended.ok()) {
        [[maybe_unused]] const auto rolledBack = transactions.rollback();
        return eve::editing::failed<TransactionReceipt>(appended.code(), RuleId("test.physics.append"),
                                                        "Could not append collider operation");
    }
    return transactions.commit();
}

}  // namespace

TEST_CASE("editor.physics.collider_schema_tracks_backend_dimensionality") {
    PhysicsColliderTarget target2d("fixture", 2);
    PhysicsColliderTarget target3d("shape", 3);
    const auto            schema2d = target2d.schema(colliderSelection(target2d));
    const auto            schema3d = target3d.schema(colliderSelection(target3d));
    CHECK_EQ(schema2d.typeId, std::string("physics.collider2d"));
    CHECK_EQ(schema3d.typeId, std::string("physics.collider3d"));
    REQUIRE(schema2d.find(PropertyPath("shape.kind")));
    REQUIRE(schema3d.find(PropertyPath("shape.kind")));
    CHECK_EQ(schema2d.find(PropertyPath("shape.kind"))->enumItems.size(), static_cast<std::size_t>(4));
    CHECK_EQ(schema3d.find(PropertyPath("shape.kind"))->enumItems.size(), static_cast<std::size_t>(6));
    CHECK_EQ(target2d.describe().type, std::string("physics-collider-2d"));
    CHECK_EQ(target3d.describe().type, std::string("physics-collider-3d"));
}

TEST_CASE("editor.physics.collider_edits_validate_and_undo") {
    PhysicsColliderTarget   target("shape", 3);
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    const SelectionSnapshot selection = colliderSelection(target);

    auto invalid = target.makeSet(selection, PropertyPath("shape.size"), EditorValue::Array{1.0, -2.0, 3.0},
                                  PropertySetMode::Absolute);
    CHECK_EQ(static_cast<int>(invalid.code()), static_cast<int>(EditorStatus::Rejected));
    invalid = target.makeSet(selection, PropertyPath("material.friction"), 1.5, PropertySetMode::Absolute);
    CHECK_EQ(static_cast<int>(invalid.code()), static_cast<int>(EditorStatus::Rejected));

    auto shape = target.makeSet(selection, PropertyPath("shape.kind"), "capsule", PropertySetMode::Absolute);
    REQUIRE(shape.ok());
    REQUIRE(commitCollider(target, transactions, shape.value(), "collider.shape").ok());
    auto radius = target.makeSet(selection, PropertyPath("shape.radius"), 0.75, PropertySetMode::Absolute);
    REQUIRE(radius.ok());
    REQUIRE(commitCollider(target, transactions, radius.value(), "collider.radius").ok());
    CHECK(target.read(selection, PropertyPath("shape.radius")).value == EditorValue(0.75));
    REQUIRE(transactions.undo().ok());
    CHECK(target.read(selection, PropertyPath("shape.radius")).value == EditorValue(0.5));
    REQUIRE(transactions.redo().ok());
    CHECK_EQ(target.validate().size(), static_cast<std::size_t>(1));
}

TEST_CASE("editor.physics.collider_snapshot_is_versioned_atomic_and_diagnostic") {
    PhysicsColliderTarget   source("source", 3);
    const SelectionSnapshot selection = colliderSelection(source);
    auto mesh = source.makeSet(selection, PropertyPath("shape.kind"), "triangle-mesh", PropertySetMode::Absolute);
    REQUIRE(mesh.ok());
    REQUIRE(source.applyDomainOperation(mesh.value()).ok());
    CHECK_EQ(source.validate().size(), static_cast<std::size_t>(1));

    PhysicsColliderTarget restored("restored", 3);
    REQUIRE(restored.loadSnapshot(source.snapshotValue()).ok());
    CHECK(restored.read(colliderSelection(restored), PropertyPath("shape.kind")).value == EditorValue("triangle-mesh"));

    PhysicsColliderTarget wrongDimension("wrong", 2);
    CHECK_EQ(static_cast<int>(wrongDimension.loadSnapshot(source.snapshotValue()).code()),
             static_cast<int>(EditorStatus::Rejected));
    CHECK(wrongDimension.read(colliderSelection(wrongDimension), PropertyPath("shape.kind")).value ==
          EditorValue("box"));

    EditorValue unknownVersion = source.snapshotValue();
    auto*       unknownRoot    = unknownVersion.getIf<EditorValue::Object>();
    REQUIRE(unknownRoot != nullptr);
    unknownRoot->at("schemaVersion")               = int64_t{2};
    const Revision revisionBeforeRejectedMigration = restored.revision();
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(unknownVersion).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(restored.revision(), revisionBeforeRejectedMigration);
    CHECK(restored.read(colliderSelection(restored), PropertyPath("shape.kind")).value == EditorValue("triangle-mesh"));
}

TEST_CASE("editor.physics.joint_exposes_body_anchor_limit_motor_and_break_authoring") {
    PhysicsJointTarget      target("suspension");
    const SelectionSnapshot selection = jointSelection(target);
    CHECK_EQ(target.schema(selection).typeId, std::string("physics.joint3d"));
    CHECK(target.schema(selection).find(PropertyPath("body.a")) != nullptr);
    CHECK(target.schema(selection).find(PropertyPath("joint.axis")) != nullptr);
    CHECK(target.schema(selection).find(PropertyPath("motor.force")) != nullptr);
    CHECK(target.schema(selection).find(PropertyPath("break.torque")) != nullptr);
    CHECK_EQ(target.validate().size(), static_cast<std::size_t>(1));

    for (const auto& [path, value] : {std::pair{PropertyPath("body.a"), EditorValue("vehicle")},
                                      {PropertyPath("body.b"), EditorValue("wheel")},
                                      {PropertyPath("joint.kind"), EditorValue("wheel")},
                                      {PropertyPath("limit.enabled"), EditorValue(true)},
                                      {PropertyPath("limit.minimum"), EditorValue(2.0)},
                                      {PropertyPath("limit.maximum"), EditorValue(1.0)}}) {
        auto operation = target.makeSet(selection, path, value, PropertySetMode::Absolute);
        REQUIRE(operation.ok());
        REQUIRE(target.applyDomainOperation(operation.value()).ok());
    }
    CHECK_EQ(target.validate().size(), static_cast<std::size_t>(1));

    auto fixLimit = target.makeSet(selection, PropertyPath("limit.maximum"), 3.0, PropertySetMode::Absolute);
    REQUIRE(fixLimit.ok());
    REQUIRE(target.applyDomainOperation(fixLimit.value()).ok());
    CHECK(target.validate().empty());
    CHECK_EQ(static_cast<int>(target.snapshotValue().type()), static_cast<int>(EditorValue::Type::Object));
}

// editor_plugin_permissions.cpp
using namespace eve::editor;

TEST_CASE("editor.plugins.permissions_are_narrow_reversible_and_default_to_ask") {
    PluginPermissionTarget permissions("project-plugins");
    CHECK_EQ(
        static_cast<int>(permissions.makeSet({StableId("broad"), "importer", "filesystem.write", "/", "allow"}).code()),
        static_cast<int>(EditorStatus::Rejected));
    PluginPermissionGrant grant{StableId("assets"), "model-importer", "filesystem.read", "assets/models", "allow"};
    auto                  set = permissions.makeSet(grant);
    REQUIRE(set.ok());
    CHECK(permissions.applyDomainOperation(set.value()).ok());
    CHECK_EQ(permissions.decision("model-importer", "filesystem.read", "assets/models"), std::string("allow"));
    CHECK_EQ(permissions.decision("model-importer", "network.connect", "example.com"), std::string("ask"));
    DomainOperation undo = set.value();
    undo.type            = set.value().inverseType;
    undo.payload         = set.value().inverse;
    CHECK(permissions.applyDomainOperation(undo).ok());
    CHECK(permissions.grants().empty());
}

TEST_CASE("editor.plugins.permission_snapshot_is_atomic_and_rejects_duplicate_semantics") {
    PluginPermissionTarget permissions("project-plugins");
    auto set = permissions.makeSet({StableId("one"), "sync", "network.connect", "api.example.com:443", "ask"});
    REQUIRE(set.ok());
    CHECK(permissions.applyDomainOperation(set.value()).ok());
    EditorValue invalid = permissions.snapshotValue();
    auto*       root    = invalid.getIf<EditorValue::Object>();
    REQUIRE(root);
    auto* grants = root->at("grants").getIf<EditorValue::Array>();
    REQUIRE(grants);
    grants->push_back(EditorValue::Object{{"id", "two"},
                                          {"plugin", "sync"},
                                          {"capability", "network.connect"},
                                          {"scope", "api.example.com:443"},
                                          {"decision", "allow"}});
    CHECK_EQ(static_cast<int>(permissions.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(permissions.grants().size(), 1U);
}

// editor_procgen_script.cpp
TEST_CASE("editor.procgen.script_editor_script_loads_schema_and_keeps_failed_preview") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.procgen", "Test Procgen Editor");
        created <- eve.ProcgenEditorModule().create("test.asset.forest");
        pcg <- created.value;
        configured <- pcg.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        panel0 <- workspace.getPanelId(0);
        cap0 <- workspace.getPanelCapability(0);
        ctx3 <- workspace.getPanelContext(3);
        schema <- [
            { key="seed", kind="int", min=1, max=9999, defaultValue=42, label="Seed" },
            { key="density", kind="float", min=0.1, max=1.0, step=0.05, defaultValue=0.8, label="Density" }
        ];
        loaded <- pcg.loadModule("game:/generators/forest.nut", "forest", "Forest", "points", schema);
        procgen <- eve.Procgen();
        pointsResult <- procgen.newPointSet();
        points <- pointsResult.value;
        points.add(1.0, 0.0, 2.0);
        points.add(3.0, 0.0, 4.0);
        published <- pcg.publishPreview(points, "trees", pcg.getRevision());
        beforePoints <- pcg.getPointCount();
        density <- pcg.setFloat("density", 0.4);
        failed <- pcg.failPreview("boom", pcg.getRevision());
        afterFail <- pcg.getPointCount();
        previewRev <- pcg.getPreviewRevision();
        undoResult <- pcg.undo();
        restoredDensity <- pcg.getFloat("density");
        rangeReject <- pcg.setFloat("density", 2.0);
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 4);
    CHECK_EQ(vm.find("panel0").toString(), std::string("procgen.modules"));
    CHECK_EQ(vm.find("cap0").toString(), std::string("procgen.script"));
    CHECK_EQ(vm.find("ctx3").toString(), std::string("stages"));
    CHECK(vm.find("loaded").toTable().get<bool>("ok"));
    CHECK(vm.find("published").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("beforePoints").toInt(), 2);
    CHECK(vm.find("density").toTable().get<bool>("ok"));
    CHECK(vm.find("failed").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("afterFail").toInt(), 2);
    CHECK(vm.find("undoResult").toTable().get<bool>("ok"));
    CHECK(std::abs(vm.find("restoredDensity").toFloat() - 0.8f) < 0.0001f);
    CHECK(!vm.find("rangeReject").toTable().get<bool>("ok"));
}

// editor_project_settings.cpp
using namespace eve::editor;

namespace {
SelectionSnapshot selection(const ProjectSettingsTarget& target) {
    SelectionSnapshot result;
    SelectionItem     item;
    item.target = TargetId(target.targetId());
    item.item   = StableId(target.targetId().value());
    result.items.push_back(item);
    result.primary = item;
    return result;
}
}  // namespace

TEST_CASE("editor.settings.secret_references_are_redacted_and_raw_values_rejected") {
    ProjectSettingsTarget settings("project", defaultProjectSettingsSchema());
    const auto            selected = selection(settings);
    auto raw = settings.makeSet(selected, PropertyPath("network.auth-token"), "plain-token", PropertySetMode::Absolute);
    CHECK_EQ(static_cast<int>(raw.code()), static_cast<int>(EditorStatus::Rejected));
    auto secret = settings.makeSet(selected, PropertyPath("network.auth-token"), "secret://project/network-token",
                                   PropertySetMode::Absolute);
    REQUIRE(secret.ok());
    CHECK(settings.applyDomainOperation(secret.value()).ok());
    auto read = settings.read(selected, PropertyPath("network.auth-token"));
    CHECK(read.value == EditorValue("secret://redacted"));
    const EditorValue snapshot = settings.snapshotValue();
    const auto*       root     = snapshot.getIf<EditorValue::Object>();
    REQUIRE(root);
    const auto* values = root->at("values").getIf<EditorValue::Object>();
    REQUIRE(values);
    CHECK(values->at("network.auth-token") == EditorValue("secret://project/network-token"));
}

TEST_CASE("editor.settings_changes_are_reversible_restart_aware_and_atomic") {
    ProjectSettingsTarget settings("project", defaultProjectSettingsSchema());
    const auto            selected = selection(settings);
    auto change = settings.makeSet(selected, PropertyPath("content.asset-root"), "content", PropertySetMode::Absolute);
    REQUIRE(change.ok());
    CHECK(settings.applyDomainOperation(change.value()).ok());
    CHECK_EQ(settings.pendingRestart().size(), 1U);
    DomainOperation undo = change.value();
    undo.type            = change.value().inverseType;
    undo.payload         = change.value().inverse;
    CHECK(settings.applyDomainOperation(undo).ok());
    CHECK(settings.read(selected, PropertyPath("content.asset-root")).value == EditorValue("assets"));

    const auto  validSnapshot = settings.snapshotValue();
    EditorValue invalid       = validSnapshot;
    auto*       root          = invalid.getIf<EditorValue::Object>();
    REQUIRE(root);
    auto* values = root->at("values").getIf<EditorValue::Object>();
    REQUIRE(values);
    (*values)["unknown.setting"] = true;
    CHECK_EQ(static_cast<int>(settings.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK(settings.read(selected, PropertyPath("content.asset-root")).value == EditorValue("assets"));
}
}  // namespace test_editor_material_target

// Contract section: editor mesh modifier editor
#include "PrivateTestScope.h"

#include "editing/EditingGraph.h"
#include "procgen/editing/MeshModifierGraph.h"
#include "procgen/editor/MeshModifierEditor.h"
// editor_mesh_modifier_editor.cpp
namespace test_editor_mesh_modifier_editor {
// editor_mesh_modifier_editor.cpp
using namespace eve;

TEST_CASE("editor.meshModifierEditor.composesAllToolsAndTracksExternalRevisions") {
    editor::EditorWorkspace            workspace("mesh", "Mesh");
    procgen_editor::MeshModifierEditor controller("mesh.main");
    REQUIRE(controller.configureWorkspace(workspace).ok());
    CHECK_EQ(workspace.getPanelCount(), 8);
    CHECK_EQ(workspace.getActivePanel(), "meshModifier.viewport");
    REQUIRE(controller.activateTool(workspace, "spline").ok());
    CHECK_EQ(workspace.getActivePanel(), "meshModifier.spline");
    CHECK_EQ(workspace.getMode(), "meshModifier.spline");
    REQUIRE(controller.activateTool(workspace, "merge").ok());
    CHECK_EQ(workspace.getActivePanel(), "meshModifier.merge");
    REQUIRE(controller.activateTool(workspace, "adhere").ok());
    CHECK_EQ(workspace.getActivePanel(), "meshModifier.adhere");
    REQUIRE(controller.observeRevision("graph", 4).ok());
    CHECK_EQ(controller.graphRevision(), std::uint64_t{4});
    CHECK(!controller.observeRevision("graph", 3).ok());
    CHECK(!controller.activateTool(workspace, "unknown").ok());
}

// editor_mesh_modifier_graph.cpp
using namespace eve::editing;
using namespace eve::procgen_editing;

namespace {

eve::procgen::MeshBuild triangle() {
    eve::procgen::MeshBuild mesh;
    mesh.addVertex(0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f);
    mesh.addVertex(1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 0.f);
    mesh.addVertex(0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f);
    mesh.addTriangle(0, 1, 2);
    return mesh;
}

}  // namespace

TEST_CASE("editor.meshModifierGraph.reflectsCompilesAndPreviewsFusedGraph") {
    MeshModifierGraphDomain domain;
    auto                    input  = domain.makeNode(GraphNodeId("input"), "mesh.input");
    auto                    move   = domain.makeNode(GraphNodeId("move"), "deform.transform");
    auto                    twist  = domain.makeNode(GraphNodeId("twist"), "deform.twist");
    auto                    output = domain.makeNode(GraphNodeId("output"), "mesh.output");
    REQUIRE(input.ok());
    REQUIRE(move.ok());
    REQUIRE(twist.ok());
    REQUIRE(output.ok());
    auto* moveProperties = move.value().properties.getIf<EditorValue::Object>();
    REQUIRE(moveProperties != nullptr);
    (*moveProperties)["x"] = EditorValue(3.0);

    GraphDocument document;
    CHECK(document.createNode(input.value()).ok());
    CHECK(document.createNode(move.value()).ok());
    CHECK(document.createNode(twist.value()).ok());
    CHECK(document.createNode(output.value()).ok());
    const auto connect = [&](const char* edge, const char* from, const char* to) {
        const auto* source = document.findPin(GraphPinId(from));
        const auto* target = document.findPin(GraphPinId(to));
        REQUIRE(source != nullptr);
        REQUIRE(target != nullptr);
        CHECK(document.connect({StableId(edge), source->id, target->id}, domain.canConnect(*source, *target)).ok());
    };
    connect("e0", "input.out", "move.in0");
    connect("e1", "move.out", "twist.in0");
    connect("e2", "twist.out", "output.in0");
    const auto snapshot = document.snapshot(domain.domain());
    const auto compiled = domain.compile(snapshot);
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Applied));

    const auto preview = domain.preview(snapshot, "input", triangle(), "output");
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(preview.mesh.getVertexCount(), 3);
    CHECK(std::abs(preview.mesh.getPositionX(0) - 3.f) < 1e-6f);
    CHECK_EQ(preview.fusedOperationCount, 2);
}

TEST_CASE("editor.meshModifierGraph.rejectsWrongDomainAndPinTypes") {
    MeshModifierGraphDomain domain;
    GraphDocumentData       wrong;
    wrong.domain        = "procgen.point";
    const auto compiled = domain.compile(wrong);
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Rejected));

    GraphPinRecord scalar{GraphPinId("scalar"), GraphNodeId("a"), "float", GraphPinDirection::Output};
    GraphPinRecord mesh{GraphPinId("mesh"), GraphNodeId("b"), "mesh", GraphPinDirection::Input};
    const auto     decision = domain.canConnect(scalar, mesh);
    CHECK(!decision.allowed);
    CHECK(!decision.diagnostics.empty());
}

TEST_CASE("editor.meshModifierGraph.compileRejectsDisconnectedInputsAndCycles") {
    MeshModifierGraphDomain domain;
    GraphDocument           disconnected;
    auto                    output = domain.makeNode(GraphNodeId("output"), "mesh.output");
    REQUIRE(output.ok());
    CHECK(disconnected.createNode(output.value()).ok());
    auto compiled = domain.compile(disconnected.snapshot(domain.domain()));
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Rejected));

    GraphDocument cyclic;
    auto          a = domain.makeNode(GraphNodeId("a"), "deform.bend");
    auto          b = domain.makeNode(GraphNodeId("b"), "deform.twist");
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(cyclic.createNode(a.value()).ok());
    CHECK(cyclic.createNode(b.value()).ok());
    const auto* aOut = cyclic.findPin(GraphPinId("a.out"));
    const auto* aIn  = cyclic.findPin(GraphPinId("a.in0"));
    const auto* bOut = cyclic.findPin(GraphPinId("b.out"));
    const auto* bIn  = cyclic.findPin(GraphPinId("b.in0"));
    REQUIRE(aOut != nullptr);
    REQUIRE(aIn != nullptr);
    REQUIRE(bOut != nullptr);
    REQUIRE(bIn != nullptr);
    CHECK(cyclic.connect({StableId("e0"), aOut->id, bIn->id}, domain.canConnect(*aOut, *bIn)).ok());
    CHECK(cyclic.connect({StableId("e1"), bOut->id, aIn->id}, domain.canConnect(*bOut, *aIn)).ok());
    compiled = domain.compile(cyclic.snapshot(domain.domain()));
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Rejected));
}
}  // namespace test_editor_mesh_modifier_editor

// Contract section: editor network telemetry
#include "PrivateTestScope.h"

#include "network/editing/NetworkTelemetry.h"
// editor_network_telemetry.cpp
namespace test_editor_network_telemetry {
using namespace eve::network_editing;
TEST_CASE("editor.network.telemetry_computes_rates_and_bounds_history") {
    NetworkTelemetryModel  model(2);
    NetworkTelemetrySample a;
    a.revision               = 1;
    a.timeSeconds            = 1;
    a.sentBytes              = 100;
    a.receivedBytes          = 200;
    a.completions            = 10;
    a.errors                 = 1;
    NetworkTelemetrySample b = a;
    b.revision               = 2;
    b.timeSeconds            = 3;
    b.sentBytes              = 500;
    b.receivedBytes          = 1000;
    b.completions            = 14;
    b.errors                 = 2;
    REQUIRE(model.ingest(a).ok());
    REQUIRE(model.ingest(b).ok());
    CHECK_EQ(model.samples().back().sendBytesPerSecond, 200.0);
    CHECK_EQ(model.samples().back().receiveBytesPerSecond, 400.0);
    CHECK_EQ(model.samples().back().errorRate, .25);
    NetworkTelemetrySample c = b;
    c.revision               = 3;
    c.timeSeconds            = 4;
    c.sentBytes              = 600;
    REQUIRE(model.ingest(c).ok());
    CHECK_EQ(model.samples().size(), std::size_t{2});
    CHECK_EQ(static_cast<int>(model.ingest(c).code()), static_cast<int>(EditorStatus::Conflict));
}
TEST_CASE("editor.network.telemetry_handles_reset_and_reports_health") {
    NetworkTelemetryModel  model;
    NetworkTelemetrySample a;
    a.revision    = 4;
    a.timeSeconds = 1;
    a.sentBytes   = 1000;
    a.completions = 100;
    a.errors      = 1;
    NetworkTelemetrySample reset;
    reset.revision       = 5;
    reset.timeSeconds    = 2;
    reset.queuedTcpBytes = 600 * 1024;
    reset.watchedTcp     = 4097;
    REQUIRE(model.ingest(a).ok());
    REQUIRE(model.ingest(reset).ok());
    CHECK_EQ(model.samples().back().sendBytesPerSecond, 0.0);
    const auto diagnostics = model.diagnostics();
    CHECK_EQ(diagnostics.size(), std::size_t{2});
}
}  // namespace test_editor_network_telemetry

// Contract section: editor offscreen preview
#include "PrivateTestScope.h"

#include <memory>
#include <stdexcept>
#include "graphics/Canvas.h"
#include "graphics/ICanvasFactory.h"
#include "graphics/ICanvasTarget.h"
#include "graphics/editing/OffscreenPreview.h"
#include "image/ImageData.h"
#include "spritestack/editing/SpriteStackTarget.h"
#include "virtualgeometry/editing/VirtualGeometryTarget.h"
// editor_offscreen_preview.cpp
namespace test_editor_offscreen_preview {
// editor_offscreen_preview.cpp
using namespace eve::graphics_editing;
using namespace eve::editing;

namespace {
class FakeCanvas final : public eve::graphics::Canvas {
public:
    FakeCanvas(int width, int height) : width_(width), height_(height) {}
    int                     getWidth() const override { return width_; }
    int                     getHeight() const override { return height_; }
    eve::graphics::Texture* getTexture() override { return nullptr; }
    void clear(std::optional<eve::graphics::Color>, std::optional<int>, std::optional<double>) override {}
    eve::graphics::Color   getPixel(int, int) override { return {}; }
    eve::image::ImageData* newImageData() override { return new eve::image::ImageData(width_, height_); }
    void                   draw(eve::graphics::Graphics*, const glm::mat4&) const override {}
    void                   draw(eve::graphics::Canvas*, const glm::mat4&) const override {}

private:
    int width_  = 0;
    int height_ = 0;
};

class FakeCanvasFactory final : public eve::graphics::ICanvasFactory {
public:
    eve::graphics::Canvas* newCanvas(int width, int height) override {
        canvas = std::make_unique<FakeCanvas>(width, height);
        return canvas.get();
    }
    std::unique_ptr<FakeCanvas> canvas;
};
class FakeCanvasTarget final : public eve::graphics::ICanvasTarget {
public:
    void setCanvas(eve::graphics::Canvas* value) override {
        current = value;
        ++bindings;
    }
    eve::graphics::Canvas* getCanvas() const override { return current; }
    eve::graphics::Canvas* current  = nullptr;
    int                    bindings = 0;
};
}  // namespace

TEST_CASE("editor.preview.offscreen_artifacts_are_bounded_revision_safe_and_releasable") {
    FakeCanvasFactory               factory;
    FakeCanvasTarget                targets;
    auto*                           graphicsToken = reinterpret_cast<eve::graphics::Graphics*>(&factory);
    GraphicsOffscreenPreviewService service(graphicsToken, &factory, &targets);
    int                             draws    = 0;
    auto                            rendered = service.render({StableId("material"), 5, 64, 32, {0.0, 0.0, 0.0, 1.0}},
                                                              [&](eve::graphics::Graphics* graphics, eve::graphics::Canvas* canvas) {
                                       REQUIRE(graphics);
                                       REQUIRE(canvas);
                                       ++draws;
                                       return eve::editing::applied<void>();
                                   });
    REQUIRE(rendered.ok());
    CHECK_EQ(draws, 1);
    CHECK_EQ(rendered.value().width, 64);
    CHECK_EQ(targets.bindings, 2);
    CHECK(targets.current == nullptr);
    auto pixels = service.image(rendered.value().handle, 5);
    REQUIRE(pixels.ok());
    REQUIRE(pixels.value());
    CHECK_EQ(pixels.value()->getWidth(), 64);
    CHECK_EQ(static_cast<int>(service.image(rendered.value().handle, 6).code()),
             static_cast<int>(EditorStatus::Conflict));
    CHECK(service.release(rendered.value().handle).ok());
    CHECK_EQ(static_cast<int>(service.image(rendered.value().handle, 5).code()),
             static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(
        static_cast<int>(
            service.render({StableId("huge"), 1, 5000, 1}, [](auto*, auto*) { return eve::editing::applied<void>(); })
                .code()),
        static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(service
                                  .render({StableId("throw"), 2, 32, 32},
                                          [](auto*, auto*) -> Result<void> { throw std::runtime_error("draw failed"); })
                                  .code()),
             static_cast<int>(EditorStatus::Failed));
    CHECK(targets.current == nullptr);
    CHECK_EQ(targets.bindings, 4);
}

// editor_spritestack_target.cpp
using namespace eve::spritestack_editing;
using namespace eve::editing;

namespace {
SelectionSnapshot select(const SpriteStackDocumentTarget& target) {
    SelectionSnapshot s;
    s.channel = "spritestack";
    s.items.push_back({SelectionDomain::Asset, TargetId(target.targetId()), StableId(target.targetId().value()),
                       "spritestack.asset"});
    return s;
}
void apply(SpriteStackDocumentTarget& target, Result<DomainOperation> operation) {
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}
class MissingModelResolver final : public ISpriteStackModelResolver {
public:
    Result<eve::model3d::ModelData*> resolveModel(const std::string&) const override {
        return eve::editing::failed<eve::model3d::ModelData*>(EditorStatus::NotFound, RuleId("test.model"),
                                                              "missing model");
    }
};
}  // namespace

TEST_CASE("editor.spritestack.preset_is_reversible_and_atomic") {
    SpriteStackDocumentTarget target("stack");
    const auto                selection = select(target);
    auto layers = target.makeSet(selection, PropertyPath("bake.layers"), int64_t{8}, PropertySetMode::Absolute);
    REQUIRE(layers.ok());
    REQUIRE(target.applyDomainOperation(layers.value()).ok());
    CHECK_EQ(target.value().bake.layerCount, 8);
    DomainOperation undo = layers.value();
    undo.payload         = layers.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(target.value().bake.layerCount, 16);
    const auto  before  = target.snapshotValue();
    EditorValue invalid = before;
    auto*       root    = invalid.getIf<EditorValue::Object>();
    auto*       content = (*root)["content"].getIf<EditorValue::Object>();
    (*content)["width"] = int64_t{100000};
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
}

TEST_CASE("editor.spritestack.bake_is_deterministic_and_reports_layer_artifacts") {
    SpriteStackDocumentTarget target("stack");
    const auto                selection = select(target);
    apply(target, target.makeSet(selection, PropertyPath("bake.layers"), int64_t{6}, PropertySetMode::Absolute));
    apply(target, target.makeSet(selection, PropertyPath("bake.width"), int64_t{32}, PropertySetMode::Absolute));
    apply(target, target.makeSet(selection, PropertyPath("bake.height"), int64_t{24}, PropertySetMode::Absolute));
    SpriteStackBakeRuntime runtime;
    auto                   first = runtime.bake(target);
    REQUIRE(first.ok());
    CHECK_EQ(first.value().size(), static_cast<std::size_t>(6));
    for (const auto& layer : first.value()) {
        CHECK_EQ(layer.width, 32);
        CHECK_EQ(layer.height, 24);
        CHECK(layer.checksum != 0);
        CHECK(layer.alphaCoverage > 0);
    }
    const auto artifacts = first.value();
    auto       second    = runtime.bake(target);
    REQUIRE(second.ok());
    for (std::size_t i = 0; i < artifacts.size(); ++i) CHECK_EQ(second.value()[i].checksum, artifacts[i].checksum);
}

TEST_CASE("editor.spritestack.failed_rebake_preserves_generation_and_stale_publish_fails_first") {
    SpriteStackDocumentTarget target("stack");
    SpriteStackBakeRuntime    runtime;
    auto                      baked = runtime.bake(target);
    REQUIRE(baked.ok());
    const auto revision  = runtime.revision();
    const auto checksum  = baked.value().front().checksum;
    const auto selection = select(target);
    apply(target, target.makeSet(selection, PropertyPath("source.kind"), "model", PropertySetMode::Absolute));
    apply(target, target.makeSet(selection, PropertyPath("source.value"), "missing.glb", PropertySetMode::Absolute));
    MissingModelResolver resolver;
    CHECK_EQ(static_cast<int>(runtime.bake(target, &resolver).code()), static_cast<int>(EditorStatus::NotFound));
    CHECK_EQ(runtime.revision(), revision);
    CHECK_EQ(runtime.layers().front()->getWidth(), 128);
    CHECK(checksum != 0);
    CHECK_EQ(static_cast<int>(runtime.publish(nullptr, target.revision()).code()),
             static_cast<int>(EditorStatus::Conflict));
}

// editor_virtualgeometry_target.cpp
using namespace eve::virtualgeometry_editing;
using namespace eve::editing;

namespace {
SelectionSnapshot select(const VirtualGeometryDocumentTarget& target) {
    SelectionSnapshot s;
    s.channel = "virtualgeometry";
    s.items.push_back({SelectionDomain::Asset, TargetId(target.targetId()), StableId(target.targetId().value()),
                       "virtualgeometry.import"});
    return s;
}
void apply(VirtualGeometryDocumentTarget& target, Result<DomainOperation> operation) {
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}
class GridResolver final : public IVirtualGeometryMeshResolver {
public:
    Result<VirtualGeometryMeshData> resolve(const std::string& id) const override {
        if (id != "grid")
            return eve::editing::failed<VirtualGeometryMeshData>(EditorStatus::NotFound, RuleId("test.mesh"),
                                                                 "missing mesh");
        VirtualGeometryMeshData mesh;
        constexpr int           size = 20;
        for (int y = 0; y <= size; ++y)
            for (int x = 0; x <= size; ++x)
                mesh.positions.insert(mesh.positions.end(), {static_cast<float>(x), 0.f, static_cast<float>(y)});
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const std::uint32_t a = y * (size + 1) + x, b = a + 1, c = a + size + 1, d = c + 1;
                mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
            }
        return eve::editing::applied<VirtualGeometryMeshData>(std::move(mesh));
    }
};
}  // namespace

TEST_CASE("editor.virtualgeometry.import_preset_is_reversible_and_atomic") {
    VirtualGeometryDocumentTarget target("vg");
    const auto                    selection = select(target);
    auto edit = target.makeSet(selection, PropertyPath("cluster.maxTriangles"), int64_t{64}, PropertySetMode::Absolute);
    REQUIRE(edit.ok());
    REQUIRE(target.applyDomainOperation(edit.value()).ok());
    CHECK_EQ(target.value().builder.maxTrianglesPerCluster, 64);
    DomainOperation undo = edit.value();
    undo.payload         = edit.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(target.value().builder.maxTrianglesPerCluster, 124);
    const auto  before        = target.snapshotValue();
    EditorValue invalid       = before;
    auto*       root          = invalid.getIf<EditorValue::Object>();
    auto*       content       = (*root)["content"].getIf<EditorValue::Object>();
    (*content)["mergeFactor"] = int64_t{20};
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
}

TEST_CASE("editor.virtualgeometry.build_is_deterministic_and_emits_lod_cost_curve") {
    VirtualGeometryDocumentTarget target("vg");
    const auto                    selection = select(target);
    apply(target, target.makeSet(selection, PropertyPath("source.asset"), "grid", PropertySetMode::Absolute));
    apply(target, target.makeSet(selection, PropertyPath("preview.samples"), int64_t{12}, PropertySetMode::Absolute));
    GridResolver                resolver;
    VirtualGeometryBuildRuntime runtime;
    auto                        first = runtime.build(target, resolver);
    REQUIRE(first.ok());
    CHECK(first.value().clusters > 0);
    CHECK(first.value().maxLod > 0);
    CHECK(first.value().roots > 0);
    CHECK_EQ(first.value().lodCurve.size(), static_cast<std::size_t>(12));
    CHECK(first.value().lodCurve.front().distance < first.value().lodCurve.back().distance);
    CHECK(first.value().lodCurve.front().triangles >= first.value().lodCurve.back().triangles);
    const auto checksum = first.value().checksum;
    auto       second   = runtime.build(target, resolver);
    REQUIRE(second.ok());
    CHECK_EQ(second.value().checksum, checksum);
}

TEST_CASE("editor.virtualgeometry.failed_rebuild_preserves_previous_generation") {
    VirtualGeometryDocumentTarget target("vg");
    const auto                    selection = select(target);
    apply(target, target.makeSet(selection, PropertyPath("source.asset"), "grid", PropertySetMode::Absolute));
    GridResolver                resolver;
    VirtualGeometryBuildRuntime runtime;
    REQUIRE(runtime.build(target, resolver).ok());
    const auto* asset    = runtime.asset();
    const auto  revision = runtime.revision();
    apply(target, target.makeSet(selection, PropertyPath("source.asset"), "missing", PropertySetMode::Absolute));
    CHECK_EQ(static_cast<int>(runtime.build(target, resolver).code()), static_cast<int>(EditorStatus::NotFound));
    CHECK(runtime.asset() == asset);
    CHECK_EQ(runtime.revision(), revision);
}
}  // namespace test_editor_offscreen_preview

// Contract section: editor particle graph
#include "PrivateTestScope.h"

#include <algorithm>
#include "particles/editing/ParticleGraph.h"
// editor_particle_graph.cpp
namespace test_editor_particle_graph {
using namespace eve::particles_editing;
using namespace eve::editing;

namespace {

GraphDocumentData particleGraph(ParticleGraphDomain& domain, int bufferSize = 1000) {
    auto emission  = domain.makeNode(GraphNodeId("emission"), "emission");
    auto motion    = domain.makeNode(GraphNodeId("motion"), "motion");
    auto collision = domain.makeNode(GraphNodeId("collision"), "collision");
    auto renderer  = domain.makeNode(GraphNodeId("renderer"), "renderer");
    auto output    = domain.makeNode(GraphNodeId("output"), "output");
    REQUIRE(emission.ok());
    REQUIRE(motion.ok());
    REQUIRE(collision.ok());
    REQUIRE(renderer.ok());
    REQUIRE(output.ok());
    auto* emissionProperties = emission.value().properties.getIf<EditorValue::Object>();
    auto* outputProperties   = output.value().properties.getIf<EditorValue::Object>();
    REQUIRE(emissionProperties);
    REQUIRE(outputProperties);
    (*emissionProperties)["rate"]     = 100.0;
    (*emissionProperties)["lifetime"] = EditorValue::Array{1.0, 2.0};
    (*outputProperties)["bufferSize"] = bufferSize;

    GraphDocument graph;
    REQUIRE(graph.createNode(emission.value()).ok());
    REQUIRE(graph.createNode(motion.value()).ok());
    REQUIRE(graph.createNode(collision.value()).ok());
    REQUIRE(graph.createNode(renderer.value()).ok());
    REQUIRE(graph.createNode(output.value()).ok());
    const std::vector<std::pair<const char*, const char*>> links = {{"emission.out", "motion.in"},
                                                                    {"motion.out", "collision.in"},
                                                                    {"collision.out", "renderer.in"},
                                                                    {"renderer.out", "output.in"}};
    int                                                    index = 0;
    for (const auto& [fromId, toId] : links) {
        const GraphPinRecord* from = graph.findPin(GraphPinId(fromId));
        const GraphPinRecord* to   = graph.findPin(GraphPinId(toId));
        REQUIRE(from);
        REQUIRE(to);
        REQUIRE(
            graph
                .connect({StableId("edge-" + std::to_string(index++)), from->id, to->id}, domain.canConnect(*from, *to))
                .ok());
    }
    return graph.snapshot(domain.domain());
}

GraphNodeRecord* findNode(GraphDocumentData& graph, const char* type) {
    const auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(),
                                    [&](const GraphNodeRecord& node) { return node.type == type; });
    return found == graph.nodes.end() ? nullptr : &*found;
}

}  // namespace

TEST_CASE("editor.particles.graph_compiles_real_emitter_modules") {
    ParticleGraphDomain              domain;
    const GraphDocumentData          graph    = particleGraph(domain);
    const ParticleGraphCompileResult compiled = domain.compile(graph);
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(compiled.documentRevision, graph.revision);
    const auto* config = compiled.configuration.getIf<EditorValue::Object>();
    REQUIRE(config);
    CHECK(config->contains("emission"));
    CHECK(config->contains("motion"));
    CHECK(config->contains("collision"));
    CHECK(config->contains("renderer"));
    CHECK(config->contains("output"));
}

TEST_CASE("editor.particles.preview_is_revision_tagged_and_budget_bounded") {
    ParticleGraphDomain     domain;
    const GraphDocumentData graph   = particleGraph(domain, 150);
    const auto              preview = domain.preview(graph, 10.0, 1.0 / 60.0, 500, 5);
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(preview.documentRevision, graph.revision);
    CHECK_EQ(preview.spawnedParticles, 500);
    CHECK_EQ(preview.droppedParticles, 500);
    CHECK_EQ(preview.peakLiveParticles, 150);
    CHECK(!preview.diagnostics.empty());

    const auto invalid = domain.preview(graph, -1.0);
    CHECK_EQ(static_cast<int>(invalid.status), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(domain.preview(graph, 601.0).status), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(domain.preview(graph, 1.0, 1.0e-9).status), static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.particles.graph_rejects_cycles_missing_modules_and_bad_properties") {
    ParticleGraphDomain domain;
    GraphDocumentData   graph = particleGraph(domain);
    graph.schemaVersion       = 99;
    CHECK_EQ(static_cast<int>(domain.compile(graph).status), static_cast<int>(EditorStatus::Unsupported));

    graph = particleGraph(domain);
    graph.nodes.erase(std::remove_if(graph.nodes.begin(), graph.nodes.end(),
                                     [](const GraphNodeRecord& node) { return node.type == "renderer"; }),
                      graph.nodes.end());
    CHECK_EQ(static_cast<int>(domain.compile(graph).status), static_cast<int>(EditorStatus::Failed));

    graph                     = particleGraph(domain);
    GraphNodeRecord* emission = findNode(graph, "emission");
    REQUIRE(emission);
    auto* properties = emission->properties.getIf<EditorValue::Object>();
    REQUIRE(properties);
    (*properties)["rate"] = -1.0;
    CHECK_EQ(static_cast<int>(domain.compile(graph).status), static_cast<int>(EditorStatus::Failed));

    graph = particleGraph(domain);
    graph.edges.push_back({StableId("cycle"), GraphPinId("renderer.out"), GraphPinId("motion.in")});
    CHECK_EQ(static_cast<int>(domain.compile(graph).status), static_cast<int>(EditorStatus::Failed));
}
}  // namespace test_editor_particle_graph

// Contract section: editor pcg graph
#include "PrivateTestScope.h"

#include "procgen/PointGraph.h"
#include "procgen/editing/PcgGraph.h"
// editor_pcg_graph.cpp
namespace test_editor_pcg_graph {
using namespace eve::procgen_editing;
using namespace eve::editing;

TEST_CASE("editor.pcgGraph.reflectsNodesAndCompilesPointGraphAsset") {
    PcgPointGraphDomain domain;
    auto                input = domain.makeNode(GraphNodeId("input"), "input");
    auto                prune = domain.makeNode(GraphNodeId("prune"), "self.prune");
    REQUIRE(input.ok());
    REQUIRE(prune.ok());
    CHECK_EQ(input.value().pins.size(), size_t(1));
    CHECK_EQ(prune.value().pins.size(), size_t(2));
    const auto* properties = prune.value().properties.getIf<EditorValue::Object>();
    REQUIRE(bool(properties));
    CHECK_EQ(*properties->at("radius").getIf<double>(), 1.0);

    GraphDocument graph;
    CHECK(graph.createNode(input.value()).ok());
    CHECK(graph.createNode(prune.value()).ok());
    EditorValue::Object radiusBinding;
    radiusBinding["node"] = EditorValue("prune");
    radiusBinding["key"]  = EditorValue("radius");
    EditorValue::Object parameters;
    parameters["pruneRadius"] = EditorValue(std::move(radiusBinding));
    CHECK(graph.setParameters(EditorValue(std::move(parameters))).ok());
    const GraphPinRecord* from = graph.findPin(GraphPinId("input.out"));
    const GraphPinRecord* to   = graph.findPin(GraphPinId("prune.in0"));
    REQUIRE(bool(from));
    REQUIRE(bool(to));
    CHECK(graph.connect({StableId("edge"), from->id, to->id}, domain.canConnect(*from, *to)).ok());

    const PcgGraphCompileResult compiled = domain.compile(graph.snapshot(domain.domain()));
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Applied));
    CHECK(compiled.definition.find("EVPCG_POINT_GRAPH 1") == 0);

    eve::procgen::PointGraph runtime;
    CHECK(runtime.deserializeDefinition(compiled.definition));
    CHECK_EQ(runtime.getParameterCount(), 1);
    CHECK_EQ(runtime.getParameterName(0), std::string("pruneRadius"));
    CHECK_EQ(runtime.getParameterKind("pruneRadius"), std::string("float"));
    eve::procgen::PointSet points;
    points.add(0.f, 0.f, 0.f);
    points.add(0.5f, 0.f, 0.f);
    CHECK(runtime.setNodePoints("input", &points));
    eve::procgen::PointSet* result = runtime.execute("prune");
    REQUIRE(bool(result));
    CHECK_EQ(result->getCount(), 1);
    delete result;

    const auto preview = domain.preview(graph.snapshot(domain.domain()), "input", &points, "prune");
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(preview.outputCount, 1);
    CHECK_EQ(preview.metrics.size(), size_t(2));
    const auto inputMetric = std::find_if(preview.metrics.begin(), preview.metrics.end(),
                                          [](const PcgGraphPreviewMetric& metric) { return metric.nodeId == "input"; });
    REQUIRE(inputMetric != preview.metrics.end());
    CHECK_EQ(inputMetric->outputCount, 2);
    CHECK_EQ(inputMetric->minX, 0.f);
    CHECK_EQ(inputMetric->maxX, 0.5f);
    CHECK_EQ(inputMetric->averageDensity, 1.f);
}

TEST_CASE("editor.pcgGraph.rejectsInvalidBlackboardParameterBindings") {
    PcgPointGraphDomain domain;
    auto                node = domain.makeNode(GraphNodeId("move"), "transform");
    REQUIRE(node.ok());
    GraphDocument graph;
    CHECK(graph.createNode(node.value()).ok());
    const Revision before = graph.revision();
    CHECK(!graph.setParameters(EditorValue("not-an-object")).ok());
    CHECK_EQ(graph.revision(), before);

    EditorValue::Object binding;
    binding["node"] = EditorValue("move");
    binding["key"]  = EditorValue("missing");
    EditorValue::Object parameters;
    parameters["bad"] = EditorValue(std::move(binding));
    CHECK(graph.setParameters(EditorValue(std::move(parameters))).ok());
    const auto compiled = domain.compile(graph.snapshot(domain.domain()));
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Failed));
    REQUIRE(!compiled.diagnostics.empty());
    CHECK_EQ(diagnosticRule(compiled.diagnostics[0]).value(), std::string("editor.pcg.invalid-parameter-binding"));
}

TEST_CASE("editor.pcgGraph.rejectsCyclesAndWrongPinTypes") {
    PcgPointGraphDomain domain;
    auto                a = domain.makeNode(GraphNodeId("a"), "jitter");
    auto                b = domain.makeNode(GraphNodeId("b"), "self.prune");
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    GraphDocument graph;
    CHECK(graph.createNode(a.value()).ok());
    CHECK(graph.createNode(b.value()).ok());
    const auto* aOut = graph.findPin(GraphPinId("a.out"));
    const auto* aIn  = graph.findPin(GraphPinId("a.in0"));
    const auto* bOut = graph.findPin(GraphPinId("b.out"));
    const auto* bIn  = graph.findPin(GraphPinId("b.in0"));
    REQUIRE(bool(aOut));
    REQUIRE(bool(aIn));
    REQUIRE(bool(bOut));
    REQUIRE(bool(bIn));
    CHECK(graph.connect({StableId("ab"), aOut->id, bIn->id}, domain.canConnect(*aOut, *bIn)).ok());
    CHECK(graph.connect({StableId("ba"), bOut->id, aIn->id}, domain.canConnect(*bOut, *aIn)).ok());
    const auto compiled = domain.compile(graph.snapshot(domain.domain()));
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Failed));
    CHECK(!compiled.diagnostics.empty());

    GraphPinRecord wrong{GraphPinId("wrong"), GraphNodeId("x"), "float", GraphPinDirection::Output};
    CHECK(!domain.canConnect(wrong, *aIn).allowed);
    CHECK(!domain.makeNode(GraphNodeId("bad"), "missing").ok());
}

TEST_CASE("editor.pcgGraph.strictlyValidatesSchemaAndNodeProperties") {
    PcgPointGraphDomain domain;
    auto                node = domain.makeNode(GraphNodeId("move"), "transform");
    REQUIRE(node.ok());

    GraphDocumentData unsupported;
    unsupported.domain        = domain.domain();
    unsupported.schemaVersion = 99;
    auto result               = domain.compile(unsupported);
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Unsupported));
    REQUIRE(!result.diagnostics.empty());
    CHECK_EQ(diagnosticRule(result.diagnostics[0]).value(), std::string("editor.pcg.unsupported-schema"));

    GraphDocument graph;
    CHECK(graph.createNode(node.value()).ok());
    EditorValue::Object properties = *node.value().properties.getIf<EditorValue::Object>();
    properties["x"]                = EditorValue("wrong-type");
    CHECK(graph.setNodeProperties(node.value().id, EditorValue(properties)).ok());
    result = domain.compile(graph.snapshot(domain.domain()));
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Failed));
    REQUIRE(!result.diagnostics.empty());
    CHECK_EQ(diagnosticRule(result.diagnostics[0]).value(), std::string("editor.pcg.property-type-mismatch"));

    properties["x"]    = EditorValue(0.0);
    properties["typo"] = EditorValue(1.0);
    CHECK(graph.setNodeProperties(node.value().id, EditorValue(properties)).ok());
    result = domain.compile(graph.snapshot(domain.domain()));
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Failed));
    REQUIRE(!result.diagnostics.empty());
    CHECK_EQ(diagnosticRule(result.diagnostics[0]).value(), std::string("editor.pcg.unknown-property"));
}

TEST_CASE("editor.pcgGraph.transactionallyMigratesLegacyPinsAndDefaults") {
    PcgPointGraphDomain domain;
    auto                input = domain.makeNode(GraphNodeId("source"), "input");
    auto                prune = domain.makeNode(GraphNodeId("prune"), "self.prune");
    REQUIRE(input.ok());
    REQUIRE(prune.ok());
    input.value().pins[0].id = GraphPinId("legacy-source-output");
    prune.value().pins[0].id = GraphPinId("legacy-prune-input");
    prune.value().pins[1].id = GraphPinId("legacy-prune-output");
    prune.value().properties = EditorValue(EditorValue::Object{});

    GraphDocumentData legacy;
    legacy.domain        = domain.domain();
    legacy.schemaVersion = 0;
    legacy.revision      = 17;
    legacy.nodes         = {input.value(), prune.value()};
    legacy.edges.push_back(
        {StableId("stable-edge"), GraphPinId("legacy-source-output"), GraphPinId("legacy-prune-input")});

    const auto migrated = domain.migrate(legacy);
    CHECK_EQ(static_cast<int>(migrated.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(migrated.fromVersion, uint32_t(0));
    CHECK_EQ(migrated.graph.schemaVersion, uint32_t(1));
    CHECK_EQ(migrated.graph.revision, Revision(17));
    REQUIRE_EQ(migrated.graph.nodes.size(), size_t(2));
    const auto* properties = migrated.graph.nodes[1].properties.getIf<EditorValue::Object>();
    REQUIRE(bool(properties));
    CHECK_EQ(*properties->at("radius").getIf<double>(), 1.0);
    REQUIRE_EQ(migrated.graph.edges.size(), size_t(1));
    CHECK_EQ(migrated.graph.edges[0].id.value(), std::string("stable-edge"));
    CHECK_EQ(migrated.graph.edges[0].from.value(), std::string("source.out"));
    CHECK_EQ(migrated.graph.edges[0].to.value(), std::string("prune.in0"));

    const auto compiled = domain.compile(legacy);
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Applied));
    CHECK(compiled.definition.find("EVPCG_POINT_GRAPH 1") == 0);
    REQUIRE(!compiled.diagnostics.empty());
    CHECK_EQ(diagnosticRule(compiled.diagnostics[0]).value(), std::string("editor.pcg.migrated-v0-v1"));

    GraphDocumentData invalid   = legacy;
    invalid.nodes[1].pins[0].id = GraphPinId("legacy-source-output");
    const auto rejected         = domain.migrate(invalid);
    CHECK_EQ(static_cast<int>(rejected.status), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(!rejected.diagnostics.empty());
    CHECK_EQ(diagnosticRule(rejected.diagnostics[0]).value(), std::string("editor.pcg.migration-duplicate-pin"));
    CHECK_EQ(legacy.schemaVersion, uint32_t(0));
}

TEST_CASE("editor.pcgGraph.boundsIsolatedPreviewPointOutputs") {
    PcgPointGraphDomain domain;
    auto                input = domain.makeNode(GraphNodeId("input"), "input");
    REQUIRE(input.ok());
    GraphDocument graph;
    CHECK(graph.createNode(input.value()).ok());
    eve::procgen::PointSet points;
    for (int index = 0; index < 1001; ++index) points.add(float(index), 0.f, 0.f);

    const auto rejected = domain.preview(graph.snapshot(domain.domain()), "input", &points, "input", 0, 1000);
    CHECK_EQ(static_cast<int>(rejected.status), static_cast<int>(EditorStatus::Failed));
    REQUIRE(!rejected.diagnostics.empty());
    CHECK(rejected.diagnostics.back().message().find("point budget") != std::string::npos);

    const auto accepted = domain.preview(graph.snapshot(domain.domain()), "input", &points, "input", 0, 1001);
    CHECK_EQ(static_cast<int>(accepted.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(accepted.outputCount, 1001);
}
}  // namespace test_editor_pcg_graph

// Contract section: editor physics asset
#include "PrivateTestScope.h"

#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Fixture.h"
#include "physics/Shape3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/editor/EditorPhysicsAsset.h"
// editor_physics_asset.cpp
namespace test_editor_physics_asset {
// editor_physics_asset.cpp
using namespace eve::editor;

namespace {
AssetRecord collider(const char* id, const char* uri, EditorValue metadata) {
    AssetRecord record;
    record.guid       = AssetGuid(id);
    record.logicalUri = uri;
    record.typeId     = "mesh";
    record.sourceUri  = std::string("source/") + id;
    record.sourceHash = "hash";
    record.importerId = "physics-test";
    record.artifacts  = {std::string("artifact/") + id};
    record.metadata   = std::move(metadata);
    return record;
}
}  // namespace

TEST_CASE("editor.physics.asset_resolver_validates_triangle_geometry_and_uri_lookup") {
    MemoryAssetDatabase database;
    EditorValue::Array  vertices{0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0};
    EditorValue::Array  indices{int64_t{0}, int64_t{1}, int64_t{2}};
    CHECK(database
              .publish(collider(
                  "triangle", "physics/triangle",
                  EditorValue::Object{{"kind", "triangle-mesh"}, {"vertices", vertices}, {"indices", indices}}))
              .ok());
    AssetDatabasePhysicsColliderResolver resolver(&database);
    auto                                 resolved = resolver.resolve("physics/triangle", "triangle-mesh");
    REQUIRE(resolved.ok());
    CHECK_EQ(resolved.value().vertices.size(), 9U);
    CHECK_EQ(resolved.value().indices.size(), 3U);
    CHECK_EQ(static_cast<int>(resolver.resolve("physics/triangle", "convex-hull").code()),
             static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.physics.asset_resolver_rejects_out_of_range_indices_and_builds_height_metadata") {
    MemoryAssetDatabase database;
    CHECK(database
              .publish(collider(
                  "bad", "physics/bad",
                  EditorValue::Object{{"kind", "triangle-mesh"},
                                      {"vertices", EditorValue::Array{0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0}},
                                      {"indices", EditorValue::Array{int64_t{0}, int64_t{1}, int64_t{9}}}}))
              .ok());
    CHECK(database
              .publish(collider("terrain", "physics/terrain",
                                EditorValue::Object{{"kind", "height-field"},
                                                    {"countX", int64_t{2}},
                                                    {"countZ", int64_t{2}},
                                                    {"cellSizeX", 1.0},
                                                    {"cellSizeZ", 2.0},
                                                    {"heights", EditorValue::Array{0.0, 1.0, 2.0, 3.0}}}))
              .ok());
    AssetDatabasePhysicsColliderResolver resolver(&database);
    CHECK_EQ(static_cast<int>(resolver.resolve("bad", "triangle-mesh").code()),
             static_cast<int>(EditorStatus::Rejected));
    auto height = resolver.resolve("terrain", "height-field");
    REQUIRE(height.ok());
    CHECK_EQ(height.value().countX, 2);
    CHECK_EQ(height.value().countZ, 2);
    CHECK_EQ(height.value().minimumHeight, 0.0F);
    CHECK_EQ(height.value().maximumHeight, 3.0F);
}

TEST_CASE("editor.physics.asset_resolver_validates_2d_polygon_and_chain_metadata") {
    MemoryAssetDatabase database;
    CHECK(database
              .publish(collider(
                  "polygon", "physics/polygon",
                  EditorValue::Object{{"kind", "polygon"},
                                      {"vertices", EditorValue::Array{0.0, 0.0, 2.0, 0.0, 2.0, 1.0, 0.0, 1.0}}}))
              .ok());
    CHECK(database
              .publish(collider("chain", "physics/chain",
                                EditorValue::Object{{"kind", "chain"},
                                                    {"loop", true},
                                                    {"vertices", EditorValue::Array{0.0, 0.0, 1.0, 0.0, 1.0, 1.0}}}))
              .ok());
    AssetDatabasePhysicsColliderResolver resolver(&database);
    auto                                 polygon = resolver.resolve("physics/polygon", "polygon");
    REQUIRE(polygon.ok());
    CHECK_EQ(polygon.value().vertices.size(), size_t{8});
    auto chain = resolver.resolve("physics/chain", "chain");
    REQUIRE(chain.ok());
    CHECK(chain.value().loop);

    CHECK(database
              .publish(collider("concave", "physics/concave",
                                EditorValue::Object{{"kind", "polygon"},
                                                    {"vertices", EditorValue::Array{0.0, 0.0, 2.0, 0.0, 1.0, 0.5, 2.0,
                                                                                    1.0, 0.0, 1.0}}}))
              .ok());
    CHECK_EQ(static_cast<int>(resolver.resolve("physics/concave", "polygon").code()),
             static_cast<int>(EditorStatus::Rejected));
}

// editor_physics_publication.cpp
using namespace eve::editor;

namespace {

class PolygonResolver final : public IPhysicsColliderAssetResolver {
public:
    Result<PhysicsColliderAssetGeometry> resolve(const std::string& reference,
                                                 const std::string& expectedKind) const override {
        if (reference != "asset://polygon" || expectedKind != "polygon")
            return eve::editing::failed<PhysicsColliderAssetGeometry>(
                EditorStatus::Conflict, RuleId("test.physics.asset"), "Unexpected 2D collider asset request");
        PhysicsColliderAssetGeometry geometry;
        geometry.kind     = "polygon";
        geometry.vertices = {0.f, 0.f, 80.f, 0.f, 80.f, 40.f, 0.f, 40.f};
        return eve::editing::applied<PhysicsColliderAssetGeometry>(std::move(geometry));
    }
};

SelectionSnapshot selection(const PhysicsColliderPublishingTarget& target) {
    SelectionSnapshot result;
    result.channel = "scene";
    result.items.push_back({SelectionDomain::Scene, TargetId(target.targetId()), StableId("collider"),
                            target.authoringTarget().describe().type});
    result.primary = result.items.front();
    return result;
}

Result<TransactionReceipt> commit(PhysicsColliderPublishingTarget& target, LocalTransactionBackend& transactions,
                                  const DomainOperation& operation, const char* id) {
    TransactionSpec specification;
    specification.id           = TransactionId(id);
    specification.label        = "Publish collider";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    auto begun                 = transactions.begin(std::move(specification));
    if (!begun.ok())
        return eve::editing::failed<TransactionReceipt>(begun.code(), RuleId("test.physics.begin"),
                                                        "Could not begin collider transaction");
    auto appended = transactions.append(operation);
    if (!appended.ok())
        return eve::editing::failed<TransactionReceipt>(appended.code(), RuleId("test.physics.append"),
                                                        "Could not append collider operation");
    return transactions.commit();
}

}  // namespace

TEST_CASE("editor.physics.live_shape_publication_swaps_after_build_and_undoes") {
    eve::physics::World3D world(0.f, -9.8f, 0.f, true);
    eve::physics::Body3D* body = world.newBody("static", 0.f, 0.f, 0.f);
    REQUIRE(body);
    eve::physics::Shape3D* initial = body->newBoxShape(1.f, 1.f, 1.f);
    REQUIRE(initial);
    const auto initialHandle = initial->runtimeHandle();

    PhysicsCollider3DRuntimeSink    sink(body, initial);
    PhysicsColliderPublishingTarget target("hero-collider", 3, &sink);
    LocalWorldAuthority             authority(&target);
    LocalTransactionBackend         transactions(&authority);
    auto change = target.authoringTarget().makeSet(selection(target), PropertyPath("shape.kind"), "sphere",
                                                   PropertySetMode::Absolute);
    REQUIRE(change.ok());
    REQUIRE(commit(target, transactions, change.value(), "physics.live.sphere").ok());
    REQUIRE(sink.shape());
    CHECK_EQ(sink.shape()->getKind(), std::string("sphere"));
    CHECK(!initial->isValid());
    CHECK(sink.shape()->runtimeHandle() != initialHandle);
    eve::physics::Shape3D* sphere = sink.shape();

    REQUIRE(transactions.undo().ok());
    REQUIRE(sink.shape());
    CHECK_EQ(sink.shape()->getKind(), std::string("box"));
    CHECK(!sphere->isValid());
    CHECK_EQ(target.authoringTarget().read(selection(target), PropertyPath("shape.kind")).value, EditorValue("box"));
}

TEST_CASE("editor.physics.live_shape_publication_rejection_preserves_old_shape_and_document") {
    eve::physics::World3D world(0.f, 0.f, 0.f, true);
    eve::physics::Body3D* body = world.newBody("static", 0.f, 0.f, 0.f);
    REQUIRE(body);
    eve::physics::Shape3D* initial = body->newBoxShape(2.f, 3.f, 4.f);
    REQUIRE(initial);
    const auto initialHandle = initial->runtimeHandle();

    PhysicsCollider3DRuntimeSink    sink(body, initial);
    PhysicsColliderPublishingTarget target("mesh-collider", 3, &sink);
    LocalWorldAuthority             authority(&target);
    LocalTransactionBackend         transactions(&authority);
    auto invalid = target.authoringTarget().makeSet(selection(target), PropertyPath("shape.kind"), "triangle-mesh",
                                                    PropertySetMode::Absolute);
    REQUIRE(invalid.ok());
    const Revision before = target.revision();
    auto           failed = commit(target, transactions, invalid.value(), "physics.live.invalid-mesh");
    CHECK_EQ(static_cast<int>(failed.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.revision(), before);
    CHECK_EQ(sink.shape(), initial);
    CHECK(initial->isValid());
    CHECK_EQ(initial->runtimeHandle(), initialHandle);
    CHECK_EQ(target.authoringTarget().read(selection(target), PropertyPath("shape.kind")).value, EditorValue("box"));
}

TEST_CASE("editor.physics.live_2d_polygon_publication_swaps_after_build_and_undoes") {
    eve::physics::World world(0.f, 980.f, true, 100.f);
    eve::physics::Body* body = world.newBody("static", 0.f, 0.f);
    REQUIRE(body);
    eve::physics::Fixture* initial = body->newRectangleFixture(50.f, 50.f);
    REQUIRE(initial);
    PolygonResolver                 resolver;
    PhysicsCollider2DRuntimeSink    sink(body, initial, &resolver);
    PhysicsColliderPublishingTarget target("hero-fixture", 2, &sink);
    LocalWorldAuthority             authority(&target);
    LocalTransactionBackend         transactions(&authority);

    auto asset = target.authoringTarget().makeSet(selection(target), PropertyPath("shape.asset"), "asset://polygon",
                                                  PropertySetMode::Absolute);
    REQUIRE(asset.ok());
    REQUIRE(commit(target, transactions, asset.value(), "physics.live.2d-asset").ok());
    eve::physics::Fixture* boxWithAsset = sink.fixture();
    REQUIRE(boxWithAsset);
    CHECK(!initial->raw());

    auto polygon = target.authoringTarget().makeSet(selection(target), PropertyPath("shape.kind"), "polygon",
                                                    PropertySetMode::Absolute);
    REQUIRE(polygon.ok());
    REQUIRE(commit(target, transactions, polygon.value(), "physics.live.2d-polygon").ok());
    eve::physics::Fixture* livePolygon = sink.fixture();
    REQUIRE(livePolygon);
    CHECK(livePolygon->raw());
    CHECK(!boxWithAsset->raw());
    CHECK_EQ(target.authoringTarget().read(selection(target), PropertyPath("shape.kind")).value,
             EditorValue("polygon"));

    REQUIRE(transactions.undo().ok());
    REQUIRE(sink.fixture());
    CHECK(sink.fixture()->raw());
    CHECK(!livePolygon->raw());
    CHECK_EQ(target.authoringTarget().read(selection(target), PropertyPath("shape.kind")).value, EditorValue("box"));
}
}  // namespace test_editor_physics_asset

// Contract section: editor procgen script editor
#include "PrivateTestScope.h"

#include "editor/EditorWorkspace.h"
#include "procgen/PointSet.h"
#include "procgen/editor/ProcgenScriptEditor.h"
// editor_procgen_script_editor.cpp
namespace test_editor_procgen_script_editor {
using eve::editor::EditorWorkspace;
using eve::procgen_editing::EditorValue;
using eve::procgen_editing::ProcgenScriptModuleSpec;
using eve::procgen_editor::ProcgenScriptEditor;

static ProcgenScriptModuleSpec forestSpec(std::string extraKey = {}) {
    ProcgenScriptModuleSpec spec;
    spec.uri         = "game:/generators/forest.nut";
    spec.id          = "forest";
    spec.displayName = "Forest scatter";
    spec.kind        = "points";
    spec.params.push_back(eve::procgen::ParamDescriptor::integer("seed", "Seed", 42, 1, 9999, 1));
    spec.params.push_back(eve::procgen::ParamDescriptor::floating("density", "Density", 0.8f, 0.1f, 1.0f, 0.05f));
    spec.params.push_back(eve::procgen::ParamDescriptor::floating("pruneRadius", "Prune", 32.f, 4.f, 80.f, 1.f));
    if (!extraKey.empty())
        spec.params.push_back(eve::procgen::ParamDescriptor::integer(extraKey, extraKey, 1, 1, 8, 1));
    return spec;
}

static eve::procgen::PointSet makePoints(int count) {
    eve::procgen::PointSet points;
    for (int i = 0; i < count; ++i) points.add(static_cast<float>(i), 0.f, static_cast<float>(i * 2));
    return points;
}

TEST_CASE("editor.procgen.script_editor_installs_workspace_and_commits_params") {
    ProcgenScriptEditor editor("preview.forest");
    EditorWorkspace     workspace("procgen.preview", "Procgen Editor");
    REQUIRE(editor.configureWorkspace(workspace).ok());
    CHECK_EQ(workspace.getPanelCount(), 4);
    CHECK_EQ(workspace.getPanelId(0), std::string("procgen.modules"));
    CHECK_EQ(workspace.getPanelCapability(0), std::string("procgen.script"));
    CHECK_EQ(workspace.getPanelContext(0), std::string("list"));
    CHECK_EQ(workspace.getPanelContext(1), std::string("preview"));
    CHECK_EQ(workspace.getPanelContext(2), std::string("inspector"));
    CHECK_EQ(workspace.getPanelContext(3), std::string("stages"));

    REQUIRE(editor.loadModule(forestSpec()).ok());
    CHECK_EQ(editor.paramCount(), 3);
    CHECK_EQ(editor.getFloat("density"), 0.8f);
    CHECK(editor.isDirty());

    eve::procgen::PointSet first = makePoints(4);
    REQUIRE(editor.publishPreview(&first, "trees", editor.revision()).ok());
    CHECK_EQ(editor.pointCount(), 4);
    CHECK_EQ(editor.previewRevision(), editor.revision());
    CHECK(!editor.isDirty());

    const auto revision = editor.revision();
    REQUIRE(editor.setFloat("density", 0.5).ok());
    CHECK(editor.isDirty());
    CHECK_EQ(editor.pointCount(), 4);
    CHECK_EQ(editor.previewRevision(), revision);
    CHECK(std::abs(editor.getFloat("density") - 0.5f) < 0.0001f);
    REQUIRE(editor.undo().ok());
    CHECK(std::abs(editor.getFloat("density") - 0.8f) < 0.0001f);
    CHECK(editor.isDirty());
}

TEST_CASE("editor.procgen.script_editor_keeps_preview_on_failed_rebuild_and_stale_publish") {
    ProcgenScriptEditor editor("preview.forest");
    REQUIRE(editor.loadModule(forestSpec()).ok());
    eve::procgen::PointSet first = makePoints(3);
    REQUIRE(editor.publishPreview(&first, "trees", editor.revision()).ok());
    const int  before          = editor.pointCount();
    const auto previewRevision = editor.previewRevision();

    REQUIRE(editor.setFloat("density", 0.4).ok());
    REQUIRE(editor.failPreview("generate exploded", editor.revision()).ok());
    CHECK_EQ(editor.pointCount(), before);
    CHECK_EQ(editor.previewRevision(), previewRevision);
    CHECK(!editor.isDirty());
    CHECK_EQ(editor.previewFailureSummary(), std::string("generate exploded"));

    eve::procgen::PointSet next  = makePoints(8);
    auto                   stale = editor.publishPreview(&next, "trees", previewRevision);
    CHECK(!stale.ok());
    CHECK_EQ(editor.pointCount(), before);
}

TEST_CASE("editor.procgen.script_editor_reload_drops_unknown_and_fills_defaults") {
    ProcgenScriptEditor editor("preview.forest");
    auto                first = forestSpec("scatter");
    REQUIRE(editor.loadModule(first).ok());
    REQUIRE(editor.setInt("scatter", 4).ok());
    CHECK_EQ(editor.getInt("scatter"), 4);

    auto second = forestSpec();
    second.params.push_back(eve::procgen::ParamDescriptor::floating("spacing", "Spacing", 28.f, 8.f, 64.f, 1.f));
    REQUIRE(editor.loadModule(second).ok());
    CHECK_EQ(editor.paramCount(), 4);
    CHECK_EQ(editor.getInt("scatter"), 0);
    CHECK(std::abs(editor.getFloat("spacing") - 28.f) < 0.0001f);
    CHECK(std::abs(editor.getFloat("density") - 0.8f) < 0.0001f);
}

TEST_CASE("editor.procgen.script_editor_rejects_out_of_range_and_over_budget") {
    ProcgenScriptEditor editor("preview.forest");
    REQUIRE(editor.loadModule(forestSpec()).ok());
    eve::procgen::PointSet first = makePoints(2);
    REQUIRE(editor.publishPreview(&first, "trees", editor.revision()).ok());
    const auto revision = editor.previewRevision();
    auto       rejected = editor.setFloat("density", 2.0);
    CHECK(!rejected.ok());
    CHECK_EQ(editor.previewRevision(), revision);
    CHECK_EQ(editor.pointCount(), 2);

    REQUIRE(editor.setPointBudget(1).ok());
    eve::procgen::PointSet huge = makePoints(3);
    REQUIRE(editor.setFloat("density", 0.2).ok());
    auto over = editor.publishPreview(&huge, "trees", editor.revision());
    CHECK(!over.ok());
    CHECK_EQ(editor.pointCount(), 2);
}
}  // namespace test_editor_procgen_script_editor

// Contract section: editor property model
#include "PrivateTestScope.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "editor/EditorCommandService.h"
#include "editor/EditorPropertyModel.h"
#include "editor/EditorPropertyPresenter.h"
#include "editor/EditorSession.h"
// editor_property_model.cpp
namespace test_editor_property_model {
// editor_property_model.cpp
using namespace eve::editor;

namespace {

PropertySchema schema() {
    PropertySchema result;
    result.typeId = "test.actor";

    PropertyDescriptor speed;
    speed.path            = PropertyPath("movement.speed");
    speed.displayNameKey  = "Speed";
    speed.type            = PropertyType::Float;
    speed.flags           = PropertyFlag::Runtime;
    speed.numeric.minimum = 0.0;
    speed.numeric.maximum = 20.0;
    result.properties.push_back(speed);

    PropertyDescriptor debug;
    debug.path           = PropertyPath("debug.name");
    debug.displayNameKey = "Debug name";
    debug.type           = PropertyType::String;
    debug.flags          = PropertyFlag::EditorOnly | PropertyFlag::ReadOnly;
    result.properties.push_back(debug);
    return result;
}

class PropertyProvider final : public IPropertyProvider {
public:
    [[nodiscard]] eve::Result<eve::Revision> currentRevision(const SelectionSnapshot&) const override {
        return eve::Result<eve::Revision>::success(eve::Revision{});
    }

    PropertySchema schema(const SelectionSnapshot&) const override { return test_editor_property_model::schema(); }

    PropertyReadResult read(const SelectionSnapshot&, const PropertyPath& path) const override {
        ++readCount;
        if (path.value() == "movement.speed") {
            if (speedState != PropertyReadState::Value) return {speedState, {}, {}};
            return {PropertyReadState::Value, EditorValue(speed), {}};
        }
        if (path.value() == "debug.name") return {PropertyReadState::Value, EditorValue(debugName), {}};
        return {};
    }

    Result<DomainOperation> makeSet(const SelectionSnapshot&, const PropertyPath&, const EditorValue&,
                                    PropertySetMode) const override {
        return eve::editing::applied<DomainOperation>({});
    }

    Result<DomainOperation> makeReset(const SelectionSnapshot&, const PropertyPath&) const override {
        return eve::editing::applied<DomainOperation>({});
    }

    double              speed      = 4.0;
    std::string         debugName  = "Actor #1";
    PropertyReadState   speedState = PropertyReadState::Value;
    mutable std::size_t readCount  = 0;
};

}  // namespace

TEST_CASE("editor.property_model_routes_runtime_writes_through_command_intent") {
    PropertyProvider    provider;
    SelectionSnapshot   selection;
    HostProfile         runtime = HostProfile::runtimeBuilder();
    EditorPropertyModel model(schema(), selection, &provider, PropertyModelSurface::Runtime, std::move(runtime));

    REQUIRE_EQ(model.schema().properties.size(), static_cast<std::size_t>(1));
    CHECK_EQ(model.schema().properties.front().path, std::string("movement.speed"));
    CHECK(model.read("debug.name") == std::nullopt);

    bool sinkCalled = false;
    model.setEditSink([&](const PropertyEditIntent& intent) {
        sinkCalled = true;
        CHECK_EQ(intent.command.value(), std::string("editor.property.set"));
        const auto* payload = intent.payload.getIf<EditorValue::Object>();
        REQUIRE(payload != nullptr);
        CHECK_EQ(*payload->at("path").getIf<std::string>(), std::string("movement.speed"));
        provider.speed = *payload->at("value").getIf<double>();
        return eve::editing::applied<void>();
    });

    std::string changedPath;
    auto        subscription =
        model.subscribe([&](const eve::property_access::PropertyChange& change) { changedPath = change.path; });
    CHECK(model.write("movement.speed", eve::Value(9.5)).ok());
    CHECK(sinkCalled);
    CHECK_EQ(provider.speed, 9.5);
    CHECK_EQ(changedPath, std::string("movement.speed"));
    REQUIRE(model.read("movement.speed").has_value());
    CHECK_EQ(*model.read("movement.speed")->getIf<double>(), 9.5);
}

TEST_CASE("editor.property_model_value_conversion_preserves_nested_data") {
    EditorValue::Object source;
    source["enabled"] = true;
    source["values"]  = EditorValue::Array{EditorValue(3), EditorValue("four")};
    const EditorValue original(std::move(source));
    CHECK(toEditorValue(toPresentationValue(original)) == original);
}

TEST_CASE("editor.property_model_reads_one_atomically_refreshed_snapshot") {
    PropertyProvider    provider;
    SelectionSnapshot   selection;
    EditorPropertyModel model(schema(), selection, &provider);

    REQUIRE(model.read("movement.speed").has_value());
    CHECK_EQ(*model.read("movement.speed")->getIf<double>(), 4.0);
    provider.speed = 8.0;
    CHECK_EQ(*model.read("movement.speed")->getIf<double>(), 4.0);

    REQUIRE(model.refresh().ok());
    CHECK_EQ(*model.read("movement.speed")->getIf<double>(), 8.0);
}

TEST_CASE("editor.property_model_notifies_value_availability_transitions") {
    PropertyProvider    provider;
    SelectionSnapshot   selection;
    EditorPropertyModel model(schema(), selection, &provider);

    std::vector<eve::property_access::PropertyChangeState> states;
    auto subscription = model.subscribe([&](const eve::property_access::PropertyChange& change) {
        if (change.path == "movement.speed") states.push_back(change.state);
    });

    provider.speedState = PropertyReadState::Mixed;
    REQUIRE(model.refresh().ok());
    CHECK(model.read("movement.speed") == std::nullopt);
    provider.speedState = PropertyReadState::Missing;
    REQUIRE(model.refresh().ok());
    CHECK(model.read("movement.speed") == std::nullopt);
    provider.speedState = PropertyReadState::Value;
    provider.speed      = 12.0;
    REQUIRE(model.refresh().ok());

    REQUIRE_EQ(states.size(), static_cast<std::size_t>(3));
    CHECK(static_cast<int>(states[0]) == static_cast<int>(eve::property_access::PropertyChangeState::Mixed));
    CHECK(static_cast<int>(states[1]) == static_cast<int>(eve::property_access::PropertyChangeState::Missing));
    CHECK(static_cast<int>(states[2]) == static_cast<int>(eve::property_access::PropertyChangeState::Value));
    CHECK_EQ(*model.read("movement.speed")->getIf<double>(), 12.0);
}

TEST_CASE("editor.property_model_queues_reentrant_snapshot_notifications") {
    PropertyProvider    provider;
    SelectionSnapshot   selection;
    EditorPropertyModel model(schema(), selection, &provider);

    std::vector<std::pair<std::string, std::uint64_t>> notifications;
    bool                                               reentered = false;
    auto subscription = model.subscribe([&](const eve::property_access::PropertyChange& change) {
        notifications.emplace_back(change.path, change.revision);
        if (change.path == "movement.speed" && !reentered) {
            reentered = true;
            REQUIRE(model.read("debug.name").has_value());
            CHECK_EQ(*model.read("debug.name")->getIf<std::string>(), std::string("Actor #2"));
            provider.speed     = 6.0;
            provider.debugName = "Actor #3";
            REQUIRE(model.refresh().ok());
        }
    });

    provider.speed     = 5.0;
    provider.debugName = "Actor #2";
    REQUIRE(model.refresh().ok());

    REQUIRE_EQ(notifications.size(), static_cast<std::size_t>(4));
    CHECK_EQ(notifications[0].first, std::string("debug.name"));
    CHECK_EQ(notifications[1].first, std::string("movement.speed"));
    CHECK_EQ(notifications[2].first, std::string("debug.name"));
    CHECK_EQ(notifications[3].first, std::string("movement.speed"));
    CHECK_EQ(notifications[0].second, notifications[1].second);
    CHECK(notifications[1].second < notifications[2].second);
    CHECK_EQ(notifications[2].second, notifications[3].second);
}

TEST_CASE("editor.property_model_runtime_surface_requires_runtime_world_feature") {
    PropertyProvider    provider;
    SelectionSnapshot   selection;
    HostProfile         denied(HostKind::RuntimeBuilder);
    EditorPropertyModel model(schema(), selection, &provider, PropertyModelSurface::Runtime, std::move(denied));

    CHECK(model.schema().properties.empty());
    CHECK(model.read("movement.speed") == std::nullopt);
    CHECK_EQ(provider.readCount, static_cast<std::size_t>(0));
}

// editor_property_v2.cpp


using namespace eve::editor;

namespace {

class TransformPropertyTarget final : public IDomainOperationTarget, public IDomainOperationTargetStaging {
public:
    explicit TransformPropertyTarget(unsigned long long initialRevision = 0) : revision_(initialRevision) {}

    TargetId      targetId() const override { return TargetId(id_); }
    std::uint64_t revision() const override { return revision_; }
    EditRegion    dirtyRegion() const override { return {}; }
    void          clearDirtyRegion() override {}

    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override {
        return std::make_unique<TransformPropertyTarget>(*this);
    }

    [[nodiscard]] Result<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override {
        auto* typed = dynamic_cast<TransformPropertyTarget*>(candidate.get());
        if (!typed)
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("scene.property.candidate"),
                                              "Compensation candidate has an incompatible type");
        position_ = typed->position_;
        revision_ = typed->revision_;
        return eve::editing::applied<void>();
    }

    Result<void> applyDomainOperation(const DomainOperation& operation) override {
        if (operation.type != "scene.transform.position.set.v1")
            return eve::editing::failed<void>(EditorStatus::Unsupported, RuleId("scene.property.unsupported"),
                                              "Unsupported property operation");
        if (!operation.payload.getIf<EditorValue::Array>())
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("scene.property.invalid-value"),
                                              "Position must be an array");
        position_ = operation.payload;
        ++revision_;
        return eve::editing::applied<void>();
    }

    const EditorValue& position() const { return position_; }

    void externalSet(EditorValue position) {
        position_ = std::move(position);
        ++revision_;
    }

private:
    std::string        id_       = "scene-property-target";
    unsigned long long revision_ = 0;
    EditorValue        position_ = EditorValue::Array{0.0, 0.0, 0.0};
};

class TransformPropertyProvider final : public IPropertyProvider {
public:
    explicit TransformPropertyProvider(TransformPropertyTarget* target) : target_(target) {}

    [[nodiscard]] eve::Result<eve::Revision> currentRevision(const SelectionSnapshot&) const override {
        return eve::Result<eve::Revision>::success(eve::Revision(target_->revision()));
    }

    PropertySchema schema(const SelectionSnapshot&) const override {
        PropertySchema result;
        result.typeId = "scene.transform";
        PropertyDescriptor position;
        position.path           = PropertyPath("transform.position");
        position.displayNameKey = "editor.transform.position";
        position.category       = "transform";
        position.type           = PropertyType::Vec3;
        position.flags          = PropertyFlag::Runtime | PropertyFlag::MultiEdit;
        position.defaultValue   = EditorValue::Array{0.0, 0.0, 0.0};
        result.properties.push_back(std::move(position));

        PropertyDescriptor debug;
        debug.path           = PropertyPath("debug.internal-name");
        debug.displayNameKey = "editor.debug.internal-name";
        debug.category       = "debug";
        debug.type           = PropertyType::String;
        debug.flags          = PropertyFlag::Advanced | PropertyFlag::EditorOnly | PropertyFlag::ReadOnly;
        debug.defaultValue   = "SceneObject";
        result.properties.push_back(std::move(debug));
        return result;
    }

    PropertyReadResult read(const SelectionSnapshot&, const PropertyPath& path) const override {
        if (path == PropertyPath("transform.position")) return {PropertyReadState::Value, target_->position(), {}};
        if (path == PropertyPath("debug.internal-name"))
            return {PropertyReadState::Value, EditorValue("SceneObject"), {}};
        return {};
    }

    Result<DomainOperation> makeSet(const SelectionSnapshot&, const PropertyPath& path, const EditorValue& value,
                                    PropertySetMode mode) const override {
        if (path != PropertyPath("transform.position") || mode != PropertySetMode::Absolute)
            return eve::editing::failed<DomainOperation>(EditorStatus::Unsupported,
                                                         RuleId("scene.property.unsupported"),
                                                         "Only absolute position changes are supported");
        DomainOperation operation;
        operation.type       = "scene.transform.position.set.v1";
        operation.target     = TargetId(target_->targetId());
        operation.payload    = value;
        operation.inverse    = target_->position();
        operation.hasInverse = true;
        operation.affectedProperties.push_back(path.value());
        return eve::editing::applied<DomainOperation>(std::move(operation));
    }

    Result<DomainOperation> makeReset(const SelectionSnapshot& selection, const PropertyPath& path) const override {
        return makeSet(selection, path, EditorValue::Array{0.0, 0.0, 0.0}, PropertySetMode::Absolute);
    }

private:
    TransformPropertyTarget* target_ = nullptr;
};

class LegacyPropertyProvider final : public IPropertyProvider {
public:
    explicit LegacyPropertyProvider(TransformPropertyProvider provider) : provider_(std::move(provider)) {}

    PropertySchema     schema(const SelectionSnapshot& selection) const override { return provider_.schema(selection); }
    PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const override {
        return provider_.read(selection, path);
    }
    Result<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                    const EditorValue& value, PropertySetMode mode) const override {
        return provider_.makeSet(selection, path, value, mode);
    }
    Result<DomainOperation> makeReset(const SelectionSnapshot& selection, const PropertyPath& path) const override {
        return provider_.makeReset(selection, path);
    }

private:
    TransformPropertyProvider provider_;
};

SelectionSnapshot sceneSelection(const TransformPropertyTarget& target) {
    SelectionSnapshot selection;
    selection.channel = "global.scene";
    SelectionItem item;
    item.domain = SelectionDomain::Scene;
    item.target = TargetId(target.targetId());
    item.item   = StableId("object-1");
    item.type   = "scene.object";
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

}  // namespace

TEST_CASE("editor.v2.property_presenters_share_schema_and_command_intent") {
    TransformPropertyTarget    target;
    TransformPropertyProvider  provider(&target);
    SelectionSnapshot          selection = sceneSelection(target);
    PropertySchema             schema    = provider.schema(selection);
    DeveloperPropertyPresenter developerPresenter;
    RuntimePropertyPresenter   runtimePresenter;
    HostProfile                runtime = HostProfile::runtimeBuilder();
    runtime.allowCommand(CommandId("editor.property.set"));

    PropertyPresentation developer = developerPresenter.present(schema, selection, provider);
    PropertyPresentation game      = runtimePresenter.present(schema, selection, provider, runtime);
    CHECK_EQ(developer.rows.size(), static_cast<std::size_t>(2));
    CHECK_EQ(game.rows.size(), static_cast<std::size_t>(1));
    CHECK_EQ(game.rows.front().descriptor.path.value(), std::string("transform.position"));

    EditorValue desired = EditorValue::Array{1.0, 2.0, 3.0};
    auto        developerIntent =
        developerPresenter.editIntent(schema, selection, PropertyPath("transform.position"), desired);
    auto runtimeIntent =
        runtimePresenter.editIntent(schema, selection, PropertyPath("transform.position"), desired, runtime);
    CHECK(developerIntent.ok());
    CHECK(runtimeIntent.ok());
    CHECK(developerIntent.value().command == runtimeIntent.value().command);
    CHECK(developerIntent.value().payload == runtimeIntent.value().payload);
    CHECK(target.position() != desired);

    auto hidden = runtimePresenter.editIntent(schema, selection, PropertyPath("debug.internal-name"),
                                              EditorValue("changed"), runtime);
    CHECK_EQ(static_cast<int>(hidden.code()), static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.v2.developer_and_game_property_ui_execute_same_command") {
    TransformPropertyTarget   target;
    TransformPropertyProvider provider(&target);
    SelectionSnapshot         selection = sceneSelection(target);
    PropertySchema            schema    = provider.schema(selection);
    LocalWorldAuthority       authority(&target);
    LocalTransactionBackend   backend(&authority);
    EditorCommandService      commands;

    CommandDescriptor descriptor;
    descriptor.id               = CommandId("editor.property.set");
    descriptor.ownerModule      = "editor";
    descriptor.requiredFeatures = HostFeature::RuntimeWorld;
    CHECK(commands
              .registerPlannedCommand(
                  descriptor,
                  [&](const CommandRequest& request) {
                      const auto* payload = request.payload.getIf<EditorValue::Object>();
                      if (!payload || !payload->contains("path") || !payload->contains("value"))
                          return eve::editing::failed<CommandPlan>(EditorStatus::Rejected,
                                                                   RuleId("editor.property.payload"),
                                                                   "Property command payload is incomplete");
                      const auto* path = payload->at("path").getIf<std::string>();
                      if (!path)
                          return eve::editing::failed<CommandPlan>(
                              EditorStatus::Rejected, RuleId("editor.property.path"), "Property path must be a string");
                      auto operation = provider.makeSet(selection, PropertyPath(*path), payload->at("value"),
                                                        PropertySetMode::Absolute);
                      if (!operation.ok()) return Result<CommandPlan>::failure(operation.status());
                      CommandPlan plan;
                      plan.operations.push_back(std::move(operation).takeValue());
                      return eve::editing::applied<CommandPlan>(std::move(plan));
                  },
                  [&](const CommandRequest&, const CommandPlan& plan) {
                      TransactionSpec specification;
                      specification.id           = TransactionId(plan.id.value());
                      specification.label        = "Set property";
                      specification.target       = plan.target;
                      specification.baseRevision = plan.baseRevision;
                      auto begun                 = backend.begin(std::move(specification));
                      if (!begun.ok())
                          return eve::editing::failed<TransactionReceipt>(begun.code(), RuleId("editor.property.begin"),
                                                                          "Could not begin property transaction");
                      for (const DomainOperation& operation : plan.operations) {
                          const auto appended = backend.append(operation);
                          if (!appended.ok())
                              return eve::editing::failed<TransactionReceipt>(
                                  EditorStatus::Failed, RuleId("editor.property.append"),
                                  "Could not append the planned property operation");
                      }
                      return backend.commit();
                  })
              .ok());

    DeveloperPropertyPresenter developerPresenter;
    RuntimePropertyPresenter   gamePresenter;
    EditorSession              developerSession;
    developerSession.setCommandService(&commands);
    developerSession.bindTarget(&target);

    EditorValue firstPosition = EditorValue::Array{4.0, 5.0, 6.0};
    auto        developerIntent =
        developerPresenter.editIntent(schema, selection, PropertyPath("transform.position"), firstPosition);
    auto developerPlan = developerSession.planCommand(developerIntent.value().command, developerIntent.value().payload);
    CHECK(developerPlan.ok());
    CHECK(developerSession.executePlan(developerPlan.value(), developerIntent.value().payload).ok());
    CHECK(target.position() == firstPosition);

    HostProfile runtime = HostProfile::runtimeBuilder();
    runtime.allowCommand(descriptor.id);
    EditorSession gameSession;
    gameSession.setHostProfile(runtime);
    gameSession.setCommandService(&commands);
    gameSession.bindTarget(&target);
    EditorValue secondPosition = EditorValue::Array{7.0, 8.0, 9.0};
    auto        gameIntent =
        gamePresenter.editIntent(schema, selection, PropertyPath("transform.position"), secondPosition, runtime);
    auto gamePlan = gameSession.planCommand(gameIntent.value().command, gameIntent.value().payload);
    CHECK(gamePlan.ok());
    CHECK(gameSession.executePlan(gamePlan.value(), gameIntent.value().payload).ok());
    CHECK(target.position() == secondPosition);
    CHECK(backend.canUndo());
}

TEST_CASE("editor.v2.property_schema_rejects_type_range_and_read_only") {
    PropertyDescriptor property;
    property.path            = PropertyPath("speed");
    property.type            = PropertyType::Float;
    property.numeric.minimum = 0.0;
    property.numeric.maximum = 10.0;
    CHECK(validatePropertyValue(property, EditorValue(5.0)).ok());
    CHECK_EQ(static_cast<int>(validatePropertyValue(property, EditorValue("fast")).code()),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(validatePropertyValue(property, EditorValue(12.0)).code()),
             static_cast<int>(EditorStatus::Rejected));
    property.flags = PropertyFlag::ReadOnly;
    CHECK_EQ(static_cast<int>(validatePropertyValue(property, EditorValue(5.0)).code()),
             static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.v2.property_model_uses_transaction_backend_for_commit_undo_and_redo") {
    TransformPropertyTarget   target;
    TransformPropertyProvider provider(&target);
    SelectionSnapshot         selection = sceneSelection(target);
    LocalWorldAuthority       authority(&target);
    LocalTransactionBackend   backend(&authority);
    EditorPropertyModel       model(provider.schema(selection), selection, &provider, PropertyModelSurface::Developer,
                                    HostProfile::developer(), &backend);

    bool legacySinkCalled = false;
    model.setEditSink([&](const PropertyEditIntent&) {
        legacySinkCalled = true;
        return eve::editing::applied<void>();
    });

    const EditorValue desired = EditorValue::Array{10.0, 11.0, 12.0};
    const auto        write   = model.write("transform.position", toPresentationValue(desired));
    CHECK(write.ok());
    CHECK(!legacySinkCalled);
    CHECK(target.position() == desired);
    CHECK(backend.canUndo());

    auto undone = model.undo();
    CHECK(undone.ok());
    CHECK(!undone.value().id.empty());
    CHECK(target.position() != desired);

    auto redone = model.redo();
    CHECK(redone.ok());
    CHECK(!redone.value().id.empty());
    CHECK(target.position() == desired);
}

TEST_CASE("editor.v2.property_model_explicit_transaction_previews_without_mutation") {
    TransformPropertyTarget   target;
    TransformPropertyProvider provider(&target);
    SelectionSnapshot         selection = sceneSelection(target);
    LocalWorldAuthority       authority(&target);
    LocalTransactionBackend   backend(&authority);
    EditorPropertyModel       model(provider.schema(selection), selection, &provider, PropertyModelSurface::Developer,
                                    HostProfile::developer(), &backend);
    const EditorValue         desired = EditorValue::Array{13.0, 14.0, 15.0};

    auto begun = model.beginTransaction("Set transform position");
    CHECK(begun.ok());
    const auto staged = model.write("transform.position", toPresentationValue(desired));
    CHECK(staged.ok());
    CHECK(target.position() != desired);

    auto previewed = model.previewTransaction();
    CHECK(previewed.ok());
    CHECK(target.position() != desired);

    auto committed = model.commitTransaction();
    CHECK(committed.ok());
    CHECK(target.position() == desired);
}

TEST_CASE("editor.v2.property_model_failed_commit_keeps_target_unchanged_and_is_discardable") {
    TransformPropertyTarget   target;
    TransformPropertyProvider provider(&target);
    SelectionSnapshot         selection = sceneSelection(target);
    ReadOnlyAuthority         authority;
    LocalTransactionBackend   backend(&authority);
    EditorPropertyModel       model(provider.schema(selection), selection, &provider, PropertyModelSurface::Developer,
                                    HostProfile::developer(), &backend);
    const EditorValue         before = target.position();

    const auto failed = model.write("transform.position", toPresentationValue(EditorValue::Array{16.0, 17.0, 18.0}));
    CHECK(!failed.ok());
    CHECK(target.position() == before);
    CHECK(backend.active());

    auto discarded = model.rollbackTransaction();
    CHECK(discarded.ok());
    CHECK(target.position() == before);
    CHECK(!backend.active());
}

TEST_CASE("editor.v2.property_model_binds_non_zero_provider_revision") {
    TransformPropertyTarget   target(41);
    TransformPropertyProvider provider(&target);
    SelectionSnapshot         selection = sceneSelection(target);
    LocalWorldAuthority       authority(&target);
    LocalTransactionBackend   backend(&authority);
    EditorPropertyModel       model(provider.schema(selection), selection, &provider, PropertyModelSurface::Developer,
                                    HostProfile::developer(), &backend);

    CHECK_EQ(model.targetRevision().value(), static_cast<std::uint64_t>(41));
    const auto write = model.write("transform.position", toPresentationValue(EditorValue::Array{1.0, 2.0, 3.0}));
    CHECK(write.ok());
    CHECK_EQ(model.targetRevision().value(), static_cast<std::uint64_t>(42));
}

TEST_CASE("editor.v2.property_model_rejects_external_change_until_refresh_rebase") {
    TransformPropertyTarget   target(7);
    TransformPropertyProvider provider(&target);
    SelectionSnapshot         selection = sceneSelection(target);
    LocalWorldAuthority       authority(&target);
    LocalTransactionBackend   backend(&authority);
    EditorPropertyModel       model(provider.schema(selection), selection, &provider, PropertyModelSurface::Developer,
                                    HostProfile::developer(), &backend);

    const EditorValue external = EditorValue::Array{20.0, 21.0, 22.0};
    target.externalSet(external);
    const EditorValue beforeRejectedWrite = target.position();
    const auto rejected = model.write("transform.position", toPresentationValue(EditorValue::Array{8.0, 9.0, 10.0}));
    CHECK(!rejected.ok());
    CHECK_EQ(eve::property_access::writeRule(rejected), std::string("editor.property.revision-conflict"));
    CHECK(target.position() == beforeRejectedWrite);
    CHECK_EQ(model.targetRevision().value(), static_cast<std::uint64_t>(7));

    const auto refreshed = model.refresh();
    CHECK(refreshed.ok());
    CHECK_EQ(model.targetRevision().value(), static_cast<std::uint64_t>(8));
    CHECK(model.read("transform.position") == std::optional<eve::Value>(toPresentationValue(external)));

    const auto accepted = model.write("transform.position", toPresentationValue(EditorValue::Array{8.0, 9.0, 10.0}));
    CHECK(accepted.ok());
    CHECK_EQ(model.targetRevision().value(), static_cast<std::uint64_t>(9));
}

TEST_CASE("editor.v2.property_model_external_change_conflict_preserves_failed_transaction_state") {
    TransformPropertyTarget   target(12);
    TransformPropertyProvider provider(&target);
    SelectionSnapshot         selection = sceneSelection(target);
    LocalWorldAuthority       authority(&target);
    LocalTransactionBackend   backend(&authority);
    EditorPropertyModel       model(provider.schema(selection), selection, &provider, PropertyModelSurface::Developer,
                                    HostProfile::developer(), &backend);

    const auto begun = model.beginTransaction("stale property edit");
    CHECK(begun.ok());
    const auto staged = model.write("transform.position", toPresentationValue(EditorValue::Array{30.0, 31.0, 32.0}));
    CHECK(staged.ok());
    const EditorValue external = EditorValue::Array{40.0, 41.0, 42.0};
    target.externalSet(external);

    const auto failed = model.commitTransaction();
    CHECK(!failed.ok());
    CHECK_EQ(static_cast<int>(failed.code()), static_cast<int>(EditorStatus::Conflict));
    CHECK(target.position() == external);
    CHECK(backend.active());

    const auto discarded = model.rollbackTransaction();
    CHECK(discarded.ok());
    CHECK(target.position() == external);
    CHECK(!backend.active());
    CHECK_EQ(model.targetRevision().value(), static_cast<std::uint64_t>(12));
}

TEST_CASE("editor.v2.legacy_property_provider_fails_closed_without_implicit_zero_revision") {
    TransformPropertyTarget   target(0);
    TransformPropertyProvider revisionAware(&target);
    LegacyPropertyProvider    legacy(std::move(revisionAware));
    SelectionSnapshot         selection = sceneSelection(target);
    EditorPropertyModel       model(legacy.schema(selection), selection, &legacy);
    model.setEditSink([](const PropertyEditIntent&) { return eve::editing::applied<void>(); });

    const auto rejected = model.write("transform.position", toPresentationValue(EditorValue::Array{2.0, 3.0, 4.0}));
    CHECK(!rejected.ok());
    CHECK_EQ(eve::property_access::writeRule(rejected), std::string("editor.property.revision-unsupported"));
    const EditorValue expected = EditorValue::Array{2.0, 3.0, 4.0};
    CHECK(target.position() != expected);
}
}  // namespace test_editor_property_model

// Contract section: editor runtime manifest v2
#include "PrivateTestScope.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include "PathBesideSource.h"
#include "common/ScriptError.h"
#include "common/SquirrelBinding.h"
#include "editor/EditCommand.h"
#include "editor/Editor.h"
#include "editor/EditorDiskDocumentStore.h"
#include "editor/EditorProtocol.h"
#include "editor/EditorRuntimeManifest.h"
#include "editor/EditorSelection.h"
#include "editor/EditorSimulationPreview.h"
#include "editor/EditorValue.h"
#include "editor/ScriptEditorTool.h"
#include "graphics/lighting/editing/SnowFieldTarget.h"
#include "map/level/editing/FieldTargets.h"
#include "weather/SnowField.h"
// editor_runtime_manifest_v2.cpp
namespace test_editor_runtime_manifest_v2 {
// editor_runtime_manifest_v2.cpp
using namespace eve::editor;

namespace {

class RuntimeParkExtension final : public IGameEditorExtension {
public:
    std::string ownerModule() const override { return "park.runtime"; }
    void        registerEditor(IEditorExtensionRegistry& registry) override {
        CommandDescriptor command;
        command.id               = CommandId("park.place-tree");
        command.displayName      = "Place tree";
        command.requiredFeatures = HostFeature::RuntimeWorld;
        CHECK(registry
                         .registerCommand(
                      std::move(command),
                      [](const CommandContext&, const EditorValue&) {
                          return eve::editing::applied<EditorValue>(EditorValue("tree"));
                      },
                      ExtensionAudience::Player)
                         .ok());
        CHECK(
            registry.registerTool({ToolId("park.tree-tool"), {}, "Tree", "Build", {}, ExtensionAudience::Player}).ok());
        CHECK(registry.registerRule({RuleId("park.path-rule"), {}, ExtensionAudience::Player}).ok());
    }
};

AssetRecord runtimeAsset(const char* guid, const char* uri) {
    AssetRecord record;
    record.guid       = AssetGuid(guid);
    record.logicalUri = uri;
    record.typeId     = "park.asset";
    return record;
}

}  // namespace

TEST_CASE("editor.v2.runtime_manifest_is_explicit_and_closes_asset_dependencies") {
    EditorCommandService    commands;
    EditorExtensionRegistry extensions(&commands);
    RuntimeParkExtension    park;
    REQUIRE(extensions.load(park).ok());

    MemoryAssetDatabase assets;
    REQUIRE(assets
                .publish(runtimeAsset("park", "content://Park"),
                         {{AssetGuid("park"), AssetGuid("tree"), DependencyKind::Hard, {}},
                          {AssetGuid("park"), AssetGuid("source"), DependencyKind::EditorOnly, {}}})
                .ok());
    REQUIRE(assets.publish(runtimeAsset("tree", "content://Tree")).ok());
    REQUIRE(assets.publish(runtimeAsset("source", "content://TreeSource")).ok());

    RuntimeEditorManifest manifest;
    manifest.commands   = {CommandId("park.place-tree")};
    manifest.tools      = {ToolId("park.tree-tool")};
    manifest.rules      = {RuleId("park.path-rule")};
    manifest.rootAssets = {AssetGuid("park")};
    auto package        = RuntimeEditorPublisher().publish(manifest, commands, extensions, assets);
    REQUIRE(package.ok());
    CHECK(package.value().profile.allowsCommand(CommandId("park.place-tree")));
    CHECK(!package.value().profile.allowsCommand(CommandId("developer.delete-source")));
    CHECK_EQ(package.value().tools.size(), static_cast<std::size_t>(1));
    CHECK_EQ(package.value().rules.size(), static_cast<std::size_t>(1));
    CHECK_EQ(package.value().assetClosure.size(), static_cast<std::size_t>(2));
}

TEST_CASE("editor.v2.runtime_manifest_rejects_developer_features") {
    EditorCommandService    commands;
    EditorExtensionRegistry extensions(&commands);
    MemoryAssetDatabase     assets;
    RuntimeEditorManifest   manifest;
    manifest.features = HostFeature::RuntimeWorld | HostFeature::ArbitraryScript;
    auto result       = RuntimeEditorPublisher().publish(manifest, commands, extensions, assets);
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(EditorStatus::Rejected));
}

// editor_script_v2.cpp
using namespace eve::editor;

TEST_CASE("editor.v2.script_discovers_plans_and_executes_registered_command") {
    Editor* editor   = Editor::create();
    auto&   commands = editor->commandService();

    const CommandId commandId("test.script.place-attraction");
    commands.unregisterCommand(commandId);
    CommandDescriptor descriptor;
    descriptor.id          = commandId;
    descriptor.ownerModule = "test.script";
    descriptor.displayName = "Place Attraction";
    descriptor.category    = "Build";

    bool plannerSawScriptSource = false;
    int  executedAmount         = 0;
    CHECK(commands
              .registerPlannedCommand(
                  descriptor,
                  [&](const CommandRequest& request) {
                      plannerSawScriptSource = request.source == CommandSource::Script;
                      const auto* object     = request.payload.getIf<EditorValue::Object>();
                      if (!object)
                          return eve::editing::failed<CommandPlan>(
                              EditorStatus::Rejected, RuleId("test.script.payload"), "Object payload required");
                      const auto amount = object->find("amount");
                      if (amount == object->end() || !amount->second.getIf<int64_t>())
                          return eve::editing::failed<CommandPlan>(EditorStatus::Rejected, RuleId("test.script.amount"),
                                                                   "Integer amount required");
                      CommandPlan plan;
                      plan.summary = "Place an attraction from a script-authored request";
                      DomainOperation operation;
                      operation.type        = "test.place";
                      operation.inverseType = "test.unplace";
                      operation.payload     = request.payload;
                      operation.hasInverse  = true;
                      plan.operations.push_back(std::move(operation));
                      return eve::editing::applied<CommandPlan>(std::move(plan));
                  },
                  [&](const CommandRequest& request, const CommandPlan& plan) {
                      const auto& object = *request.payload.getIf<EditorValue::Object>();
                      executedAmount     = static_cast<int>(*object.at("amount").getIf<int64_t>());
                      TransactionReceipt receipt;
                      receipt.id               = TransactionId(plan.id.value());
                      receipt.state            = TransactionState::Committed;
                      receipt.beforeRevision   = plan.baseRevision;
                      receipt.afterRevision    = plan.baseRevision + 1;
                      receipt.authorityReceipt = "local:test.script";
                      return eve::editing::applied<TransactionReceipt>(std::move(receipt));
                  })
              .ok());

    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        session <- editor.newSession();
        found <- false;
        for (local i = 0; i < session.getCommandCount(); ++i) {
            if (session.getCommandId(i) == "test.script.place-attraction" &&
                session.getCommandName(i) == "Place Attraction" &&
                session.getCommandCategory(i) == "Build") found = true;
        }
        planned <- session.planCommand("test.script.place-attraction", {
            amount = 7,
            placement = { x = 12.5, y = 4.0 },
            tags = ["ride", "family"]
        });
        executed <- session.executePlan(planned.planId, {});
    )"));

    CHECK(vm.find("found").toBool());
    ssq::Table planned(vm.find("planned"));
    ssq::Table executed(vm.find("executed"));
    CHECK(planned.get<bool>("accepted"));
    CHECK_EQ(planned.get<std::string>("status"), std::string("applied"));
    CHECK(executed.get<bool>("accepted"));
    CHECK_EQ(executed.get<std::string>("transactionState"), std::string("committed"));
    CHECK_EQ(executed.get<int64_t>("afterRevision"), int64_t{1});
    CHECK(plannerSawScriptSource);
    CHECK_EQ(executedAmount, 7);

    CHECK(commands.unregisterCommand(commandId, "test.script"));
}

TEST_CASE("editor.v2.script_game_injects_command_into_the_same_session_protocol") {
    Editor* editor = Editor::create();
    editor->commandService().unregisterCommand(CommandId("test.script.spawn-tree"));

    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        placed <- 0;
        registered <- editor.registerScriptCommand(
            "test.script.spawn-tree", "Spawn Tree", "Build",
            function(payload) {
                if (!("count" in payload) || payload.count <= 0) return false;
                placed += payload.count;
                return true;
            });
        session <- editor.newSession();
        planned <- session.planCommand("test.script.spawn-tree", { count = 3 });
        executed <- session.executePlan(planned.planId, {});
    )"));

    CHECK(vm.find("registered").toBool());
    CHECK_EQ(vm.find("placed").toInt(), 3);
    ssq::Table executed(vm.find("executed"));
    CHECK(executed.get<bool>("accepted"));
    CHECK_EQ(executed.get<std::string>("authorityReceipt"), std::string("script:local"));

    CHECK(editor->commandService().unregisterCommand(CommandId("test.script.spawn-tree"),
                                                     "script:test.script.spawn-tree"));
}

TEST_CASE("editor.v2.runtime_builder_quick_add_click_executes_command") {
    const std::filesystem::path sourceRoot =
        std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path().parent_path();
    const std::filesystem::path scriptPath = sourceRoot / "examples" / "editor-api-v2" / "main.nut";
    std::ifstream               input(scriptPath, std::ios::binary);
    REQUIRE(input.is_open());
    std::ostringstream source;
    source << input.rdbuf();

    const std::string harness = R"(
        eve_init <- null;
        eve_update <- null;
        eve_render <- null;
        persist <- function(key, factory) { return factory(); };
        gfx <- {
            rects = [],
            canvasDraws = 0,
            newCanvas = function(w, h) { return { width=w, height=h }; },
            setCanvas = function(canvas) {},
            drawCanvas = function(canvas, x, y, w, h) { canvasDraws += 1; },
            setBackgroundColor = function(r, g, b, a) {},
            clear = function() {},
            drawSolidRect = function(x, y, w, h, r, g, b, a) {
                rects.append({ x=x, y=y, w=w, h=h, r=r, g=g, b=b, a=a });
            }
        };
        mouse <- {
            isDown = function(button) { return false; },
            getX = function() { return 0.0; },
            getY = function() { return 0.0; }
        };
        ui <- {
            clicks = [],
            setTheme = function(name) {},
            beginBuild = function() {},
            beginWindow = function(title, id) {},
            text = function(value, id) {},
            beginRow = function(id, gap) {},
            button = function(label, id) {},
            end = function() {},
            mountBuildAs = function(name) {},
            select = function(name) {},
            setHostOverlay = function(enabled) {},
            setHostPos = function(x, y, z, w) {},
            setHostSize = function(w, h) {},
            setText = function(id, value) {},
            wantCaptureMouse = function() { return false; },
            beginFrameAndRender = function() {},
            consumeClick = function() {
                if (clicks.len() == 0) return "";
                local value = clicks[0];
                clicks.remove(0);
                return value;
            }
        };
    )";

    ssq::VM vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    const std::string executable = harness + source.str() + R"(
        eve_init();
        ui.clicks.append("editor-v2/asset-tree");
        eve_update(0.016);
        eve_render();
        quickAddObjectCount <- editorV2.objects.len();
        quickAddCredits <- editorV2.credits;
        quickAddAsset <- editorV2.objects[0].asset;
        quickAddCanvasDraws <- gfx.canvasDraws;
        quickAddRenderedTree <- false;
        foreach (rect in gfx.rects) {
            if (rect.w >= 18.0 && rect.h >= 12.0 && rect.g > 0.6)
                quickAddRenderedTree = true;
        }
        cleanupResult <- editorV2.editor.unregisterScriptCommand("park.scene.place-asset");
    )";
    vm.run(vm.compileSource(executable.c_str(), "examples/editor-api-v2/main.nut"));

    CHECK_EQ(vm.find("quickAddObjectCount").toInt(), 1);
    CHECK_EQ(vm.find("quickAddCredits").toInt(), 580);
    CHECK_EQ(vm.find("quickAddAsset").toString(), std::string("park.asset.tree"));
    CHECK_EQ(vm.find("quickAddCanvasDraws").toInt(), 1);
    CHECK(vm.find("quickAddRenderedTree").toBool());
    CHECK(vm.find("cleanupResult").toBool());
}

// editor_selection_v2.cpp
using namespace eve::editor;

TEST_CASE("editor.v2.selection_and_focus_are_presentation_neutral_channels") {
    EditorSelectionService selection;
    int                    notifications = 0;
    REQUIRE(selection.subscribe("park.hud", [&](const SelectionSnapshot&) { ++notifications; }).ok());
    SelectionItem tree{SelectionDomain::Scene, TargetId("park"), StableId("tree-1"), "park.tree"};
    auto          selected = selection.set("world", {tree}, tree);
    REQUIRE(selected.ok());
    CHECK_EQ(selected.value().sequence, static_cast<std::uint64_t>(1));
    CHECK(selection.snapshot("world").primary == tree);
    CHECK_EQ(notifications, 1);
    CHECK_EQ(static_cast<int>(selection.set("world", {}, tree).code()), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(selection.clear("world").ok());
    CHECK_EQ(notifications, 2);
    CHECK(selection.unsubscribe("park.hud"));

    EditorFocusService focus;
    auto               focused = focus.focus("world", StableId("viewport"), StableId("tree-1"));
    REQUIRE(focused.ok());
    CHECK(focus.snapshot("world").surface == StableId("viewport"));
}

// editor_session_persistence.cpp
using namespace eve::editor;

namespace {

EditorValue versionValue(int version) { return EditorValue(EditorValue::Object{{"version", EditorValue(version)}}); }

}  // namespace

TEST_CASE("editor.session.autosavesActiveDocument") {
    MemoryAtomicDocumentStore store;
    DocumentService           documents(&store);
    AutosaveService           autosave(&store, "live-project");
    const DocumentKey         key{DocumentKind::Scene, AssetGuid("live-scene")};
    auto opened = documents.open(key, "Live Scene", "content://World/Live.evscene", versionValue(0));
    REQUIRE(opened.ok());

    EditorSession session;
    session.setDocumentServices(&documents, &autosave);
    REQUIRE(session.bindDocument(opened.value().id).ok());
    session.setAutosaveInterval(0.1f);
    session.setExternalPollInterval(0.f);
    REQUIRE(session.editDocument(versionValue(1)).ok());
    session.update(0.11f);

    CHECK_EQ(session.lastAutosavedRevision(), Revision(1));
    auto draft = autosave.readDraft(opened.value().id);
    REQUIRE(draft.ok());
    CHECK(draft.value().content == versionValue(1));
    CHECK(documents.snapshot(opened.value().id).value().dirty());
}

TEST_CASE("editor.session.adoptsCleanExternalChangesAndProtectsDirtyEdits") {
    MemoryAtomicDocumentStore store;
    DocumentService           documents(&store);
    const DocumentKey         key{DocumentKind::Scene, AssetGuid("conflict-scene")};
    auto opened = documents.open(key, "Conflict Scene", "content://World/Conflict.evscene", versionValue(0));
    REQUIRE(opened.ok());

    EditorSession session;
    session.setDocumentServices(&documents);
    REQUIRE(session.bindDocument(opened.value().id).ok());
    REQUIRE(session.editDocument(versionValue(1)).ok());
    REQUIRE(session.saveDocument().ok());

    auto disk = store.read("content://World/Conflict.evscene");
    REQUIRE(disk.ok());
    REQUIRE(store
                .compareAndSwap("content://World/Conflict.evscene", disk.value().revision, disk.value().contentHash,
                                versionValue(2))
                .ok());
    REQUIRE(session.pollDocumentChanges().ok());
    CHECK(documents.content(opened.value().id).value() == versionValue(2));
    CHECK(!documents.snapshot(opened.value().id).value().dirty());

    REQUIRE(session.editDocument(versionValue(3)).ok());
    disk = store.read("content://World/Conflict.evscene");
    REQUIRE(store
                .compareAndSwap("content://World/Conflict.evscene", disk.value().revision, disk.value().contentHash,
                                versionValue(4))
                .ok());
    const auto conflict = session.pollDocumentChanges();
    CHECK_EQ(static_cast<int>(conflict.code()), static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(static_cast<int>(documents.snapshot(opened.value().id).value().state),
             static_cast<int>(DocumentState::Conflict));
    CHECK(documents.content(opened.value().id).value() == versionValue(3));
}

// editor_simulation_preview.cpp
using namespace eve::editor;

namespace {
class FakeSimulation final : public IEditorSimulationBackend {
public:
    std::unique_ptr<IEditorSimulationBackend> cloneForPreview() const override {
        return std::make_unique<FakeSimulation>(*this);
    }
    Result<void> step(std::uint64_t tick, double fixedDelta) override {
        if (tick <= tick_)
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("fake.tick"), "Ticks must increase");
        tick_ = tick;
        position_ += fixedDelta * 3.0;
        return eve::editing::applied<void>();
    }
    Result<std::vector<SimulationObjectSample>> capture() const override {
        SimulationObjectSample sample;
        sample.object    = "body-1";
        sample.positionX = position_;
        return eve::editing::applied<std::vector<SimulationObjectSample>>({sample});
    }
    double position() const { return position_; }

private:
    std::uint64_t tick_     = 0;
    double        position_ = 0.0;
};
}  // namespace

TEST_CASE("editor.simulation.preview_pauses_steps_and_bakes_without_mutating_source") {
    FakeSimulation              source;
    SimulationPreviewController controller;
    CHECK_EQ(static_cast<int>(controller.update(source).code()), static_cast<int>(EditorStatus::NoOp));
    REQUIRE(controller.setFixedDelta(0.5).ok());
    auto frame = controller.singleStep(source);
    REQUIRE(frame.ok());
    CHECK_EQ(frame.value().objects[0].positionX, 1.5);
    frame = controller.singleStep(source);
    REQUIRE(frame.ok());
    CHECK_EQ(frame.value().objects[0].positionX, 3.0);
    CHECK_EQ(source.position(), 0.0);
    auto baked = controller.bake(source, 42, 3, 10);
    CHECK_EQ(static_cast<int>(baked.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(baked.sourceRevision, 42U);
    CHECK_EQ(baked.frames.size(), 4U);
    CHECK_EQ(baked.frames.back().objects[0].positionX, 4.5);
    CHECK_EQ(source.position(), 0.0);
    CHECK_EQ(static_cast<int>(controller.bake(source, 42, 11, 10).status), static_cast<int>(EditorStatus::Rejected));
}

// editor_snow_target.cpp
using namespace eve::editor;
using namespace eve::level_editing;
using eve::lighting_editing::SnowFieldTarget;

TEST_CASE("editor.snow.live_scalar_target_supports_bilinear_preview_and_reversible_brush_edits") {
    eve::weather::SnowField field(2, 2);
    field.fill(1.0F);
    SnowFieldTarget        target("snow-depth", &field);
    ScalarFieldEditCommand command("footprint", &target);
    REQUIRE(command.record(0, 0, 0.0F));
    REQUIRE(command.record(1, 0, 0.5F));
    REQUIRE(command.apply());
    CHECK_EQ(field.height(0, 0), 0.0F);
    CHECK_EQ(target.sampleScalar(0.5F, 0.0F), 0.25F);
    CHECK(!target.dirtyRegion().empty());
    command.revert();
    CHECK_EQ(field.height(0, 0), 1.0F);
    CHECK_EQ(field.height(1, 0), 1.0F);
}

TEST_CASE("editor.v2.script_command_failure_is_not_silent") {
    eve::script::resetReportedScriptCallFailures();
    Editor*         editor = Editor::create();
    const CommandId commandId("test.script.exploding");
    editor->commandService().unregisterCommand(commandId);

    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        registered <- editor.registerScriptCommand(
            "test.script.exploding", "Explode", "Build",
            function(payload) { throw "command exploded"; });
        session <- editor.newSession();
        planned <- session.planCommand("test.script.exploding", {});
        executed <- session.executePlan(planned.planId, {});
    )"));

    REQUIRE(vm.find("registered").toBool());
    ssq::Table planned(vm.find("planned"));
    ssq::Table executed(vm.find("executed"));
    CHECK(planned.get<bool>("accepted"));
    REQUIRE(!executed.get<bool>("accepted"));
    // A callback that threw used to reach the author as "Script command callback rejected
    // the planned payload", which reads as a deliberate `return false`. The failure now
    // goes through the shared reporter as well.
    REQUIRE(eve::script::reportedScriptCallFailures() >= 1);

    CHECK(editor->commandService().unregisterCommand(commandId, "script:test.script.exploding"));
}
TEST_CASE("editor.v2.script_tool_failure_is_not_silent") {
    eve::script::resetReportedScriptCallFailures();
    Editor* editor = Editor::create();

    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    // This case makes script throw, so the VM needs the engine's error hook: a bare
    // ssq::VM keeps simplesquirrel's defaultRuntimeErrorFunc, which dereferences the
    // callback stack without checking it and crashes (see test/ScriptTest.h).
    eve::script::installScriptErrorHook(vm.getHandle());
    vm.run(vm.compileSource(R"(
        exploder <- function(payload) { throw "tool callback exploded"; };
    )"));

    std::unique_ptr<ScriptEditorTool> tool = editor->newScriptTool("test.tool.exploding", "Explode");
    tool->setActivateCallback(vm.find("exploder"));
    tool->setUpdateCallback(vm.find("exploder"));
    tool->setPointerCallback(vm.find("exploder"));

    eve::editor::EditorContext context;
    tool->activate(context);
    tool->update(context, 0.016f);
    const ToolResponse pointer = tool->pointerEvent(context, eve::editor::EditorPointerEvent{});

    // Both templates dropped the information: `invoke()` discarded the call result and
    // `invokeFlags()` read a throw as "the callback returned 0", so a broken tool callback
    // looked exactly like a tool that handled nothing.
    REQUIRE(eve::script::reportedScriptCallFailures() >= 3);
    REQUIRE(!pointer.handled);
}
}  // namespace test_editor_runtime_manifest_v2

// Contract section: editor scene component payload
#include "PrivateTestScope.h"

#include "audio/editor/EditorAudioTarget.h"
#include "graphics/material/editor/EditorMaterialTarget.h"
#include "scene/editor/EditorSceneComponentPayload.h"
#include "scene/editor/EditorSceneTarget.h"
#include "scene/loader/editing/SceneImportTarget.h"
// editor_scene_component_payload.cpp
namespace test_editor_scene_component_payload {
// editor_scene_component_payload.cpp
using namespace eve::editor;

namespace {

const EditorValue* field(const EditorValue& value, const char* name) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(name);
    return found == object->end() ? nullptr : &found->second;
}

class HealthPayloadProvider final : public ISceneComponentPayloadProvider,
                                    public IDomainOperationTarget,
                                    public IDomainOperationTargetStaging {
public:
    explicit HealthPayloadProvider(std::string id = "health-payload") : id_(std::move(id)) {}

    const std::string& componentType() const override {
        static const std::string type = "health";
        return type;
    }
    TargetId      targetId() const override { return TargetId(id_); }
    std::uint64_t revision() const override { return revision_; }
    EditRegion    dirtyRegion() const override { return {}; }
    void          clearDirtyRegion() override {}

    std::vector<SceneComponentPayloadRef> components(const TargetId& scene, const ObjectId& object) const override {
        if (object != ObjectId("hero")) return {};
        return {{scene, object, StableId("hero.health"), componentType(), generation_, revision_}};
    }

    Result<IDomainOperationTarget*> payloadOperationTarget(const SelectionSnapshot&) const override {
        return eve::editing::applied<IDomainOperationTarget*>(const_cast<HealthPayloadProvider*>(this));
    }

    std::vector<EditorDiagnostic> validateComponent(const SceneComponentPayloadRef& component) const override {
        if (component.generation != generation_)
            return {eve::editing::ruleDiagnostic(eve::DiagnosticCode::PreconditionViolation,
                                                 RuleId("test.health.stale"), DiagnosticSeverity::Error,
                                                 "Health component reference is stale")};
        return {};
    }

    eve::Result<eve::Revision> currentRevision(const SelectionSnapshot&) const override {
        return eve::Result<eve::Revision>::success(eve::Revision(revision_));
    }

    PropertySchema schema(const SelectionSnapshot&) const override {
        PropertySchema result;
        result.typeId = componentType();
        PropertyDescriptor points;
        points.path            = PropertyPath("points");
        points.type            = PropertyType::Float;
        points.defaultValue    = 100.0;
        points.numeric.minimum = 0.0;
        result.properties.push_back(std::move(points));
        return result;
    }

    PropertyReadResult read(const SelectionSnapshot& selection, const PropertyPath& path) const override {
        if (selection.items.size() != 1 || selection.items.front().item != StableId("hero.health") ||
            path != PropertyPath("points"))
            return {};
        return {PropertyReadState::Value, points_, {}};
    }

    Result<DomainOperation> makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                    const EditorValue& value, PropertySetMode mode) const override {
        if (mode != PropertySetMode::Absolute || selection.items.size() != 1 ||
            selection.items.front().item != StableId("hero.health") || path != PropertyPath("points"))
            return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("test.health.selection"),
                                                         "Invalid health component selection");
        const auto validation = validatePropertyValue(schema(selection).properties.front(), value);
        if (!validation.ok()) return Result<DomainOperation>::failure(validation.status());
        DomainOperation operation;
        operation.type       = "test.health.set.v1";
        operation.target     = TargetId(id_);
        operation.payload    = EditorValue::Object{{"component", "hero.health"}, {"value", value}};
        operation.inverse    = EditorValue::Object{{"component", "hero.health"}, {"value", points_}};
        operation.hasInverse = true;
        operation.affectedObjects.push_back({TargetId(id_), "hero.health", generation_});
        operation.affectedProperties.push_back(path.value());
        operation.mergeKey = "health:hero.health:points";
        return eve::editing::applied<DomainOperation>(std::move(operation));
    }

    Result<DomainOperation> makeReset(const SelectionSnapshot& selection, const PropertyPath& path) const override {
        return makeSet(selection, path, 100.0, PropertySetMode::Absolute);
    }

    Result<void> applyDomainOperation(const DomainOperation& operation) override {
        if (operation.target != TargetId(id_) || operation.type != "test.health.set.v1")
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("test.health.operation"),
                                              "Invalid health operation");
        const EditorValue* component   = field(operation.payload, "component");
        const EditorValue* value       = field(operation.payload, "value");
        const auto*        componentId = component ? component->getIf<std::string>() : nullptr;
        const auto*        number      = value ? value->getIf<double>() : nullptr;
        if (!componentId || *componentId != "hero.health" || !number || *number < 0.0)
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("test.health.payload"),
                                              "Invalid health payload");
        points_ = *number;
        ++revision_;
        return eve::editing::applied<void>();
    }

    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override {
        return std::make_unique<HealthPayloadProvider>(*this);
    }

    Result<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override {
        auto* typed = dynamic_cast<HealthPayloadProvider*>(candidate.get());
        if (!typed)
            return eve::editing::failed<void>(EditorStatus::Conflict, RuleId("test.health.candidate"),
                                              "Invalid health candidate");
        *this = *typed;
        return eve::editing::applied<void>();
    }

    double points() const { return points_; }
    void   invalidate() { ++generation_; }

private:
    std::string        id_;
    unsigned long long revision_   = 1;
    std::uint64_t      generation_ = 7;
    double             points_     = 80.0;
};

class RecordingAudioSink final : public IAudioSourceRuntimeSink {
public:
    Result<void> publish(const AudioSourceTarget& candidate) override {
        if (reject)
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("test.audio.runtime-rejected"),
                                              "Runtime rejected candidate");
        SelectionSnapshot selection;
        selection.items.push_back(
            {SelectionDomain::Scene, TargetId(candidate.targetId()), StableId("source"), "audio.source"});
        const auto  read  = candidate.read(selection, PropertyPath("play.volume"));
        const auto* value = read.value.getIf<double>();
        if (!value)
            return eve::editing::failed<void>(EditorStatus::Failed, RuleId("test.audio.runtime-value"),
                                              "Candidate volume is unavailable");
        volume   = *value;
        revision = candidate.revision();
        ++publishes;
        return eve::editing::applied<void>();
    }

    bool     reject    = false;
    double   volume    = 1.0;
    Revision revision  = 0;
    int      publishes = 0;
};

class RecordingMaterialSink final : public IMaterialRuntimeSink {
public:
    Result<void> publish(const MaterialDocumentTarget& candidate) override {
        if (reject)
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("test.material.runtime-rejected"),
                                              "Runtime rejected material candidate");
        SelectionSnapshot selection;
        selection.items.push_back(
            {SelectionDomain::Asset, TargetId(candidate.targetId()), StableId("material"), "graphics.material"});
        const auto  read  = candidate.read(selection, PropertyPath("shading.roughness"));
        const auto* value = read.value.getIf<double>();
        if (!value)
            return eve::editing::failed<void>(EditorStatus::Failed, RuleId("test.material.runtime-value"),
                                              "Candidate roughness is unavailable");
        roughness = *value;
        revision  = candidate.revision();
        ++publishes;
        return eve::editing::applied<void>();
    }

    bool     reject    = false;
    double   roughness = 0.45;
    Revision revision  = 0;
    int      publishes = 0;
};

Result<TransactionReceipt> commit(IDomainOperationTarget& target, LocalTransactionBackend& transactions,
                                  const DomainOperation& operation, Revision baseRevision, const char* id) {
    TransactionSpec specification;
    specification.id           = TransactionId(id);
    specification.label        = "Component payload edit";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = baseRevision;
    auto begun                 = transactions.begin(std::move(specification));
    if (!begun.ok())
        return eve::editing::failed<TransactionReceipt>(begun.code(), RuleId("test.begin"),
                                                        "Could not begin transaction");
    auto appended = transactions.append(operation);
    if (!appended.ok())
        return eve::editing::failed<TransactionReceipt>(appended.code(), RuleId("test.append"),
                                                        "Could not append operation");
    return transactions.commit();
}

}  // namespace

TEST_CASE("editor.scene.component_payload_registry_routes_schema_transactions_and_undo") {
    HealthPayloadProvider         provider;
    SceneComponentPayloadRegistry registry;
    REQUIRE(registry.registerProvider(&provider).ok());
    CHECK_EQ(static_cast<int>(registry.registerProvider(&provider).code()), static_cast<int>(EditorStatus::Conflict));

    SceneDocumentTarget scene("scene");
    scene.bindComponentPayloads(&registry);
    auto* payloads = static_cast<ISceneComponentPayloadTarget*>(
        scene.queryCapability(ISceneComponentPayloadTarget::editorCapabilityId()));
    REQUIRE(payloads);
    CHECK_EQ(scene.describe().capabilities.back(), ISceneComponentPayloadTarget::editorCapabilityId());

    auto references = payloads->componentPayloads(TargetId("scene"), ObjectId("hero"));
    REQUIRE(references.ok());
    REQUIRE_EQ(references.value().size(), size_t{1});
    CHECK_EQ(references.value().front().component, StableId("hero.health"));
    auto selection = makeSceneComponentSelection("scene-inspector", references.value(), 12);
    REQUIRE(selection.ok());

    auto properties = payloads->propertyProvider(selection.value());
    auto target     = payloads->operationTarget(selection.value());
    REQUIRE(properties.ok());
    REQUIRE(target.ok());
    CHECK_EQ(properties.value()->schema(selection.value()).typeId, std::string("health"));
    CHECK_EQ(*properties.value()->read(selection.value(), PropertyPath("points")).value.getIf<double>(), 80.0);

    auto operation =
        properties.value()->makeSet(selection.value(), PropertyPath("points"), 25.0, PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    LocalWorldAuthority     authority(target.value());
    LocalTransactionBackend transactions(&authority);
    const Revision          baseRevision = target.value()->revision();
    REQUIRE(commit(*target.value(), transactions, operation.value(), baseRevision, "health.set").ok());
    CHECK_EQ(provider.points(), 25.0);
    REQUIRE(transactions.undo().ok());
    CHECK_EQ(provider.points(), 80.0);

    auto stale = commit(*target.value(), transactions, operation.value(), baseRevision, "health.stale");
    CHECK_EQ(static_cast<int>(stale.code()), static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.scene.component_payload_registry_rejects_mixed_and_stale_references") {
    HealthPayloadProvider         provider;
    SceneComponentPayloadRegistry registry;
    REQUIRE(registry.registerProvider(&provider).ok());
    auto references = registry.componentPayloads(TargetId("scene"), ObjectId("hero"));
    REQUIRE(references.ok());
    SceneComponentPayloadRef stale = references.value().front();
    provider.invalidate();
    auto diagnostics = registry.validatePayload(stale);
    CHECK_EQ(static_cast<int>(diagnostics.code()), static_cast<int>(EditorStatus::Conflict));

    SelectionSnapshot mixed;
    mixed.items = {{SelectionDomain::Scene, TargetId("scene"), StableId("hero.health"), "health"},
                   {SelectionDomain::Scene, TargetId("scene"), StableId("hero.audio"), "audio"}};
    CHECK_EQ(static_cast<int>(registry.propertyProvider(mixed).code()), static_cast<int>(EditorStatus::Rejected));
    const auto unregisterResult = registry.unregisterProvider(&provider);
    REQUIRE_EQ(static_cast<int>(unregisterResult), static_cast<int>(SceneComponentChange::Changed));
    auto selection = makeSceneComponentSelection("scene", references.value());
    REQUIRE(selection.ok());
    CHECK_EQ(static_cast<int>(registry.propertyProvider(selection.value()).code()),
             static_cast<int>(EditorStatus::Unsupported));
}

TEST_CASE("editor.scene.component_bindings_reuse_audio_physics_and_material_targets") {
    AudioSourceTarget              audio("hero-audio");
    PhysicsColliderTarget          collider("hero-collider", 3);
    MaterialDocumentTarget         material("hero-material");
    SceneComponentPropertyBindings audioBindings("audio.source");
    SceneComponentPropertyBindings physicsBindings("physics.collider3d");
    SceneComponentPropertyBindings materialBindings("graphics.material");

    const TargetId scene("scene");
    const ObjectId hero("hero");
    REQUIRE(audioBindings
                .bind({scene, hero, StableId("hero.audio"), "audio.source", 1, 0},
                      {SelectionDomain::Scene, TargetId(audio.targetId()), StableId("source"), "audio.source"}, &audio,
                      &audio, [&] { return audio.validate(); })
                .ok());
    REQUIRE(
        physicsBindings
            .bind({scene, hero, StableId("hero.collider"), "physics.collider3d", 2, 0},
                  {SelectionDomain::Scene, TargetId(collider.targetId()), StableId("collider"), "physics.collider3d"},
                  &collider, &collider, [&] { return collider.validate(); })
            .ok());
    REQUIRE(
        materialBindings
            .bind({scene, hero, StableId("hero.material"), "graphics.material", 3, 0},
                  {SelectionDomain::Scene, TargetId(material.targetId()), StableId("material"), "graphics.material"},
                  &material, &material, [&] { return material.validate(); })
            .ok());

    SceneComponentPayloadRegistry registry;
    REQUIRE(registry.registerProvider(&audioBindings).ok());
    REQUIRE(registry.registerProvider(&physicsBindings).ok());
    REQUIRE(registry.registerProvider(&materialBindings).ok());
    auto all = registry.componentPayloads(scene, hero);
    REQUIRE(all.ok());
    REQUIRE_EQ(all.value().size(), size_t{3});
    CHECK_EQ(all.value().at(0).type, std::string("audio.source"));
    CHECK_EQ(all.value().at(1).type, std::string("graphics.material"));
    CHECK_EQ(all.value().at(2).type, std::string("physics.collider3d"));

    const auto audioRef  = all.value().at(0);
    auto       selection = makeSceneComponentSelection("inspector", {audioRef});
    REQUIRE(selection.ok());
    auto properties      = registry.propertyProvider(selection.value());
    auto operationTarget = registry.operationTarget(selection.value());
    REQUIRE(properties.ok());
    REQUIRE(operationTarget.ok());
    CHECK_EQ(properties.value()->schema(selection.value()).typeId, std::string("audio.source"));
    auto setVolume =
        properties.value()->makeSet(selection.value(), PropertyPath("play.volume"), 0.25, PropertySetMode::Absolute);
    REQUIRE(setVolume.ok());
    LocalWorldAuthority     authority(operationTarget.value());
    LocalTransactionBackend transactions(&authority);
    REQUIRE(commit(*operationTarget.value(), transactions, setVolume.value(), operationTarget.value()->revision(),
                   "component.audio.volume")
                .ok());
    CHECK_EQ(*audio
                  .read({"module",
                         {{SelectionDomain::Scene, TargetId(audio.targetId()), StableId("source"), "audio.source"}},
                         {},
                         0},
                        PropertyPath("play.volume"))
                  .value.getIf<double>(),
             0.25);
    CHECK_EQ(static_cast<int>(registry.validatePayload(audioRef).code()), static_cast<int>(EditorStatus::Conflict));
    REQUIRE(transactions.undo().ok());
    CHECK_EQ(*audio
                  .read({"module",
                         {{SelectionDomain::Scene, TargetId(audio.targetId()), StableId("source"), "audio.source"}},
                         {},
                         0},
                        PropertyPath("play.volume"))
                  .value.getIf<double>(),
             1.0);

    auto colliderSelection = makeSceneComponentSelection("inspector", {all.value().at(2)});
    REQUIRE(colliderSelection.ok());
    CHECK(registry.propertyProvider(colliderSelection.value())
              .value()
              ->schema(colliderSelection.value())
              .find(PropertyPath("material.friction")) != nullptr);
    auto materialSelection = makeSceneComponentSelection("inspector", {all.value().at(1)});
    REQUIRE(materialSelection.ok());
    CHECK(registry.propertyProvider(materialSelection.value())
              .value()
              ->schema(materialSelection.value())
              .find(PropertyPath("shading.roughness")) != nullptr);
}

TEST_CASE("editor.scene.audio_component_publication_is_staged_live_and_reversible") {
    RecordingAudioSink             sink;
    AudioSourcePublishingTarget    live("live-audio", &sink);
    SceneComponentPropertyBindings bindings("audio.source");
    REQUIRE(bindings
                .bind({TargetId("scene"), ObjectId("hero"), StableId("hero.audio"), "audio.source", 1, 0},
                      {SelectionDomain::Scene, TargetId(live.targetId()), StableId("source"), "audio.source"},
                      &live.authoringTarget(), &live, [&] { return live.authoringTarget().validate(); })
                .ok());
    SceneComponentPayloadRegistry registry;
    REQUIRE(registry.registerProvider(&bindings).ok());
    auto components = registry.componentPayloads(TargetId("scene"), ObjectId("hero"));
    REQUIRE(components.ok());
    auto selection = makeSceneComponentSelection("inspector", components.value());
    REQUIRE(selection.ok());
    auto properties      = registry.propertyProvider(selection.value());
    auto operationTarget = registry.operationTarget(selection.value());
    REQUIRE(properties.ok());
    REQUIRE(operationTarget.ok());

    auto change =
        properties.value()->makeSet(selection.value(), PropertyPath("play.volume"), 0.4, PropertySetMode::Absolute);
    REQUIRE(change.ok());
    LocalWorldAuthority     authority(operationTarget.value());
    LocalTransactionBackend transactions(&authority);
    REQUIRE(commit(*operationTarget.value(), transactions, change.value(), operationTarget.value()->revision(),
                   "audio.live.volume")
                .ok());
    CHECK_EQ(sink.volume, 0.4);
    CHECK_EQ(sink.revision, live.revision());
    CHECK_EQ(sink.publishes, 1);
    REQUIRE(transactions.undo().ok());
    CHECK_EQ(sink.volume, 1.0);
    CHECK_EQ(sink.publishes, 2);

    auto rejected =
        properties.value()->makeSet(selection.value(), PropertyPath("play.volume"), 0.2, PropertySetMode::Absolute);
    REQUIRE(rejected.ok());
    sink.reject           = true;
    const Revision before = live.revision();
    auto failed = commit(*operationTarget.value(), transactions, rejected.value(), before, "audio.live.rejected");
    CHECK_EQ(static_cast<int>(failed.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(live.revision(), before);
    CHECK_EQ(sink.volume, 1.0);
}

TEST_CASE("editor.scene.material_component_publication_is_staged_live_and_reversible") {
    RecordingMaterialSink          sink;
    MaterialPublishingTarget       live("live-material", &sink);
    SceneComponentPropertyBindings bindings("graphics.material");
    REQUIRE(bindings
                .bind({TargetId("scene"), ObjectId("hero"), StableId("hero.material"), "graphics.material", 2, 0},
                      {SelectionDomain::Asset, TargetId(live.targetId()), StableId("material"), "graphics.material"},
                      &live.authoringTarget(), &live, [&] { return live.authoringTarget().validate(); })
                .ok());
    SceneComponentPayloadRegistry registry;
    REQUIRE(registry.registerProvider(&bindings).ok());
    auto components = registry.componentPayloads(TargetId("scene"), ObjectId("hero"));
    REQUIRE(components.ok());
    auto selection = makeSceneComponentSelection("inspector", components.value());
    REQUIRE(selection.ok());
    auto properties      = registry.propertyProvider(selection.value());
    auto operationTarget = registry.operationTarget(selection.value());
    REQUIRE(properties.ok());
    REQUIRE(operationTarget.ok());

    auto change = properties.value()->makeSet(selection.value(), PropertyPath("shading.roughness"), 0.8,
                                              PropertySetMode::Absolute);
    REQUIRE(change.ok());
    LocalWorldAuthority     authority(operationTarget.value());
    LocalTransactionBackend transactions(&authority);
    REQUIRE(commit(*operationTarget.value(), transactions, change.value(), operationTarget.value()->revision(),
                   "material.live.roughness")
                .ok());
    CHECK_EQ(sink.roughness, 0.8);
    CHECK_EQ(sink.revision, live.revision());
    REQUIRE(transactions.undo().ok());
    CHECK_EQ(sink.roughness, 0.45);

    auto rejected = properties.value()->makeSet(selection.value(), PropertyPath("shading.roughness"), 0.6,
                                                PropertySetMode::Absolute);
    REQUIRE(rejected.ok());
    sink.reject           = true;
    const Revision before = live.revision();
    auto failed = commit(*operationTarget.value(), transactions, rejected.value(), before, "material.live.rejected");
    CHECK_EQ(static_cast<int>(failed.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(live.revision(), before);
    CHECK_EQ(sink.roughness, 0.45);
}

// editor_scene_import_target.cpp
using namespace eve::sceneloader_editing;
using namespace eve::editing;
namespace {
SelectionSnapshot select(const SceneImportTarget& t) {
    SelectionSnapshot s;
    s.channel = "scene-import";
    s.items.push_back({SelectionDomain::Asset, TargetId(t.targetId()), StableId(t.targetId().value()), "scene-import"});
    return s;
}
void apply(SceneImportTarget& t, Result<DomainOperation> op) {
    REQUIRE(op.ok());
    REQUIRE(t.applyDomainOperation(op.value()).ok());
}
}  // namespace
TEST_CASE("editor.scene_import.presets_expand_and_custom_edits_are_reversible") {
    SceneImportTarget target("castle.import");
    const auto        selection = select(target);
    apply(target, target.makeSet(selection, PropertyPath("source"), "castle.glb", PropertySetMode::Absolute));
    auto mobile = target.makeSet(selection, PropertyPath("preset"), "mobile", PropertySetMode::Absolute);
    REQUIRE(mobile.ok());
    REQUIRE(target.applyDomainOperation(mobile.value()).ok());
    CHECK(!target.value().importAnimations);
    CHECK(!target.value().importLights);
    CHECK(target.value().mipmaps);
    auto camera = target.makeSet(selection, PropertyPath("importCameras"), true, PropertySetMode::Absolute);
    REQUIRE(camera.ok());
    REQUIRE(target.applyDomainOperation(camera.value()).ok());
    CHECK_EQ(target.value().preset, std::string("custom"));
    DomainOperation undo = camera.value();
    undo.payload         = camera.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(target.value().preset, std::string("mobile"));
    CHECK(!target.value().importCameras);
}
TEST_CASE("editor.scene_import.snapshot_load_is_atomic_and_preflight_requires_loader") {
    SceneImportTarget target("castle.import");
    const auto        selection = select(target);
    apply(target, target.makeSet(selection, PropertyPath("source"), "castle.glb", PropertySetMode::Absolute));
    const auto  before   = target.snapshotValue();
    EditorValue invalid  = before;
    auto*       root     = invalid.getIf<EditorValue::Object>();
    auto*       content  = (*root)["content"].getIf<EditorValue::Object>();
    (*content)["preset"] = "unknown";
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
    SceneImportPreflightRuntime runtime;
    CHECK_EQ(static_cast<int>(runtime.inspect(target, nullptr).code()), static_cast<int>(EditorStatus::Rejected));
}
}  // namespace test_editor_scene_component_payload

// Contract section: editor scene runtime
#include "PrivateTestScope.h"

#include "scene/NodeDesc.h"
#include "scene/SceneHost.h"
#include "scene/editing/SceneTarget.h"
// editor_scene_runtime.cpp
namespace test_editor_scene_runtime {
using namespace eve::scene_editing;

namespace {

template <class T>
T* take(eve::Result<T*> result) {
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}

}  // namespace

TEST_CASE("editor.scene.live_host_applies_incremental_hierarchy_and_trs_without_losing_links") {
    using namespace eve::scene;
    SceneHost* host = take(SceneHost::createHost("editor-live"));
    host->setTree(node("root", {node("mesh").withPosition(1.f, 2.f, 3.f), node("socket")}));
    SceneNode* mesh     = take(host->findById("mesh"));
    int        external = 42;
    mesh->links.push_back({777, &external, 0});

    SceneHostEditorTarget target("live", host);
    auto*                 inspector =
        static_cast<ISceneComponentInspector*>(target.queryCapability(ISceneComponentInspector::editorCapabilityId()));
    REQUIRE(inspector);
    auto links = inspector->componentLinks(ObjectId("mesh"));
    REQUIRE(links.ok());
    REQUIRE_EQ(links.value().size(), size_t{1});
    CHECK(links.value().at(0).targetAlive);
    CHECK_EQ(links.value().at(0).syncMode, 0);
    auto rename = target.makeRename(ObjectId("mesh"), "Hero Mesh");
    REQUIRE(rename.ok());
    REQUIRE(target.applyDomainOperation(rename.value()).ok());
    mesh = take(host->findById("mesh"));
    CHECK_EQ(mesh->name, "Hero Mesh");
    REQUIRE_EQ(mesh->links.size(), size_t{1});
    CHECK_EQ(mesh->links[0].target, &external);

    SceneTransformValue transform{4.0, 5.0, 6.0, 0.1, 0.2, 0.3, 2.0, 3.0, 4.0};
    auto                move = target.makeSetTransform(ObjectId("mesh"), transform);
    REQUIRE(move.ok());
    REQUIRE(target.applyDomainOperation(move.value()).ok());
    mesh = take(host->findById("mesh"));
    CHECK_EQ(mesh->x, 4.f);
    CHECK_EQ(mesh->pitch, 0.1f);
    CHECK_EQ(mesh->yaw, 0.2f);
    CHECK_EQ(mesh->sx, 2.f);
    CHECK_EQ(mesh->links[0].target, &external);

    auto reparent = target.makeReparent(ObjectId("mesh"), ObjectId("socket"));
    REQUIRE(reparent.ok());
    REQUIRE(target.applyDomainOperation(reparent.value()).ok());
    CHECK_EQ(host->getParentById("mesh")->id, "socket");
}

TEST_CASE("editor.scene.live_host_staged_create_delete_and_conflict_are_atomic") {
    using namespace eve::scene;
    SceneHost* host = take(SceneHost::createHost("editor-live-create"));
    host->setTree(node("root", {node("existing")}));
    SceneHostEditorTarget target("live-create", host);

    CreateSceneObjectRequest request{ObjectId("created"), ObjectId("root"), "Created",
                                     SceneTransformValue{1.0, 0.0, 0.0}};
    auto                     create = target.makeCreate(request);
    REQUIRE(create.ok());
    REQUIRE(target.applyDomainOperation(create.value()).ok());
    CHECK(host->hasNode("created"));
    CHECK_EQ(host->getParentById("created")->id, "root");

    auto remove = target.makeDelete(ObjectId("created"));
    REQUIRE(remove.ok());
    REQUIRE(target.applyDomainOperation(remove.value()).ok());
    CHECK(!host->hasNode("created"));

    auto  staged      = target.cloneDomainState();
    auto* stagedScene = dynamic_cast<SceneHostEditorTarget*>(staged.get());
    REQUIRE(stagedScene);
    auto rename = stagedScene->makeRename(ObjectId("existing"), "Changed");
    REQUIRE(rename.ok());
    REQUIRE(stagedScene->SceneTargetBase::applyDomainOperation(rename.value()).ok());
    REQUIRE_EQ(static_cast<int>(host->renameNode("existing", "External Change")),
               static_cast<int>(eve::scene::SceneMutationStatus::Applied));
    const auto conflict = target.commitDomainState(std::move(staged));
    CHECK_EQ(static_cast<int>(conflict.code()), static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(take(host->findById("existing"))->name, "External Change");
    CHECK_EQ(target.sceneObject(ObjectId("existing")).value().name, "existing");
}
}  // namespace test_editor_scene_runtime

// Contract section: editor scene target v2
#include "PrivateTestScope.h"

// editor_scene_target_v2.cpp
namespace test_editor_scene_target_v2 {
using namespace eve::editor;

namespace {

Result<TransactionReceipt> commitOperation(SceneTargetBase& target, LocalTransactionBackend& transactions,
                                           const DomainOperation& operation, const std::string& id) {
    TransactionSpec specification;
    specification.id           = TransactionId(id);
    specification.label        = "Scene edit";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    auto begun                 = transactions.begin(std::move(specification));
    if (!begun.ok())
        return eve::editing::failed<TransactionReceipt>(begun.code(), RuleId("test.scene.begin"),
                                                        "Could not begin scene transaction");
    auto appended = transactions.append(operation);
    if (!appended.ok()) {
        auto rolledBack = transactions.rollback();
        if (!rolledBack.ok())
            return eve::editing::failed<TransactionReceipt>(rolledBack.code(), RuleId("test.scene.rollback"),
                                                            "Could not roll back scene transaction");
        return eve::editing::failed<TransactionReceipt>(appended.code(), RuleId("test.scene.append"),
                                                        "Could not append scene operation");
    }
    return transactions.commit();
}

void exerciseSceneBackend(SceneTargetBase& target, const std::string& transactionPrefix) {
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    ScenePlacementToolLogic placement;
    SceneTransformToolLogic transform;

    CreateSceneObjectRequest root;
    root.id            = ObjectId("root");
    root.name          = "Root";
    root.transform     = {0.0, 0.0, 0.0};
    auto rootOperation = placement.plan(target, root);
    CHECK(rootOperation.ok());
    CHECK(commitOperation(target, transactions, rootOperation.value(), transactionPrefix + ".root").ok());

    CreateSceneObjectRequest child;
    child.id            = ObjectId("coaster");
    child.parent        = root.id;
    child.name          = "Coaster";
    child.transform     = {1.0, 0.0, 2.0};
    auto childOperation = placement.plan(target, child);
    CHECK(childOperation.ok());
    CHECK(commitOperation(target, transactions, childOperation.value(), transactionPrefix + ".child").ok());
    CHECK_EQ(target.sceneChildren(root.id).size(), static_cast<std::size_t>(1));

    SceneTransformValue moved{8.0, 3.0, 5.0, 0.1, 0.5, -0.2, 2.0, 1.5, 0.75};
    auto                transformOperation = transform.plan(target, child.id, moved);
    CHECK(transformOperation.ok());
    CHECK(commitOperation(target, transactions, transformOperation.value(), transactionPrefix + ".move").ok());
    CHECK(target.readTransform(child.id).value() == moved);

    CHECK(transactions.undo().ok());
    CHECK(target.readTransform(child.id).value() == child.transform);
    CHECK(transactions.redo().ok());
    CHECK(target.readTransform(child.id).value() == moved);
}

TEST_CASE("editor.v2.scene_transform_supports_atomic_multi_object_trs_edits") {
    RuntimeWorldTarget      target("runtime");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    ScenePlacementToolLogic placement;
    SceneTransformToolLogic transform;

    for (const std::string id : {"left", "right"}) {
        CreateSceneObjectRequest request;
        request.id     = ObjectId(id);
        request.name   = id;
        auto operation = placement.plan(target, request);
        REQUIRE(operation.ok());
        REQUIRE(commitOperation(target, transactions, operation.value(), "create." + id).ok());
    }

    const SceneTransformValue leftValue{1.0, 2.0, 3.0, 0.0, 0.25, 0.0, 2.0, 2.0, 2.0};
    const SceneTransformValue rightValue{-1.0, 4.0, 6.0, 0.5, 0.0, 1.0, 0.5, 1.0, 1.5};
    auto                      left  = transform.plan(target, ObjectId("left"), leftValue);
    auto                      right = transform.plan(target, ObjectId("right"), rightValue);
    REQUIRE(left.ok());
    REQUIRE(right.ok());

    TransactionSpec specification;
    specification.id           = TransactionId("transform.multi");
    specification.label        = "Transform selection";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    REQUIRE(transactions.begin(std::move(specification)).ok());
    REQUIRE(transactions.append(left.value()).ok());
    REQUIRE(transactions.append(right.value()).ok());
    REQUIRE(transactions.commit().ok());
    CHECK(target.readTransform(ObjectId("left")).value() == leftValue);
    CHECK(target.readTransform(ObjectId("right")).value() == rightValue);

    REQUIRE(transactions.undo().ok());
    CHECK(target.readTransform(ObjectId("left")).value() == SceneTransformValue{});
    CHECK(target.readTransform(ObjectId("right")).value() == SceneTransformValue{});
    REQUIRE(transactions.redo().ok());
    CHECK(target.readTransform(ObjectId("left")).value() == leftValue);
    CHECK(target.readTransform(ObjectId("right")).value() == rightValue);
}

}  // namespace

TEST_CASE("editor.v2.scene_document_and_runtime_targets_are_tool_isomorphic") {
    SceneDocumentTarget document("document-scene");
    RuntimeWorldTarget  runtime("runtime-scene");
    exerciseSceneBackend(document, "document");
    exerciseSceneBackend(runtime, "runtime");

    CHECK_EQ(document.describe().type, std::string("scene-document"));
    CHECK_EQ(runtime.describe().type, std::string("runtime-world"));
    CHECK_EQ(document.describe().capabilities, runtime.describe().capabilities);
}

TEST_CASE("editor.v2.scene_targets_expose_stable_capability_ids") {
    SceneDocumentTarget target("scene");
    CHECK(target.queryCapability(ISceneHierarchyEditTarget::editorCapabilityId()) != nullptr);
    CHECK(target.queryCapability(ITransformEditTarget::editorCapabilityId()) != nullptr);
    CHECK(target.queryCapability(CapabilityId("unknown")) == nullptr);

    CreateSceneObjectRequest invalid;
    invalid.id     = ObjectId("child");
    invalid.parent = ObjectId("missing");
    auto operation = ScenePlacementToolLogic().plan(target, invalid);
    CHECK_EQ(static_cast<int>(operation.code()), static_cast<int>(EditorStatus::NotFound));
}

TEST_CASE("editor.v2.scene_create_is_reversible_through_authority") {
    RuntimeWorldTarget       target("runtime");
    LocalWorldAuthority      authority(&target);
    LocalTransactionBackend  transactions(&authority);
    CreateSceneObjectRequest request;
    request.id     = ObjectId("placed-object");
    request.name   = "Placed Object";
    auto operation = ScenePlacementToolLogic().plan(target, request);
    CHECK(operation.ok());
    CHECK(commitOperation(target, transactions, operation.value(), "place").ok());
    CHECK(target.sceneObject(request.id).ok());
    CHECK(transactions.undo().ok());
    CHECK_EQ(static_cast<int>(target.sceneObject(request.id).code()), static_cast<int>(EditorStatus::NotFound));
}

TEST_CASE("editor.v2.scene_hierarchy_edits_are_reversible_and_cycle_safe") {
    SceneDocumentTarget     target("scene");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    ScenePlacementToolLogic placement;
    SceneHierarchyToolLogic hierarchy;

    for (const auto& request : {CreateSceneObjectRequest{ObjectId("root"), {}, "Root", {}},
                                CreateSceneObjectRequest{ObjectId("group"), ObjectId("root"), "Group", {}},
                                CreateSceneObjectRequest{ObjectId("leaf"), ObjectId("group"), "Leaf", {}}}) {
        auto operation = placement.plan(target, request);
        REQUIRE(operation.ok());
        REQUIRE(commitOperation(target, transactions, operation.value(), "create." + request.id.value()).ok());
    }

    auto cycle = hierarchy.planReparent(target, ObjectId("root"), ObjectId("leaf"));
    CHECK_EQ(static_cast<int>(cycle.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(hierarchy.planDelete(target, ObjectId("group")).code()),
             static_cast<int>(EditorStatus::Rejected));

    auto rename = hierarchy.planRename(target, ObjectId("leaf"), "Camera");
    REQUIRE(rename.ok());
    REQUIRE(commitOperation(target, transactions, rename.value(), "rename").ok());
    CHECK_EQ(target.sceneObject(ObjectId("leaf")).value().name, std::string("Camera"));
    REQUIRE(transactions.undo().ok());
    CHECK_EQ(target.sceneObject(ObjectId("leaf")).value().name, std::string("Leaf"));
    REQUIRE(transactions.redo().ok());

    auto reparent = hierarchy.planReparent(target, ObjectId("leaf"), ObjectId("root"));
    REQUIRE(reparent.ok());
    REQUIRE(commitOperation(target, transactions, reparent.value(), "reparent").ok());
    CHECK(target.sceneObject(ObjectId("leaf")).value().parent == ObjectId("root"));

    auto remove = hierarchy.planDelete(target, ObjectId("leaf"));
    REQUIRE(remove.ok());
    REQUIRE(commitOperation(target, transactions, remove.value(), "delete").ok());
    CHECK_EQ(static_cast<int>(target.sceneObject(ObjectId("leaf")).code()), static_cast<int>(EditorStatus::NotFound));
    REQUIRE(transactions.undo().ok());
    CHECK_EQ(target.sceneObject(ObjectId("leaf")).value().name, std::string("Camera"));
    CHECK(target.sceneObject(ObjectId("leaf")).value().parent == ObjectId("root"));
}

TEST_CASE("editor.v2.scene_property_provider_multi_edits_generic_trs_inspector") {
    SceneDocumentTarget     target("scene");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    ScenePlacementToolLogic placement;
    for (const std::string id : {"one", "two"}) {
        CreateSceneObjectRequest request;
        request.id     = ObjectId(id);
        request.name   = id;
        auto operation = placement.plan(target, request);
        REQUIRE(operation.ok());
        REQUIRE(commitOperation(target, transactions, operation.value(), "property.create." + id).ok());
    }

    SelectionSnapshot selection;
    selection.channel = "scene";
    for (const std::string id : {"one", "two"}) {
        SelectionItem item;
        item.domain = SelectionDomain::Scene;
        item.target = TargetId(target.targetId());
        item.item   = StableId(id);
        item.type   = "scene.object";
        selection.items.push_back(item);
    }
    selection.primary = selection.items.front();

    ScenePropertyProvider provider(&target);
    CHECK_EQ(provider.schema(selection).properties.size(), static_cast<std::size_t>(3));
    CHECK_EQ(static_cast<int>(provider.read(selection, PropertyPath("transform.scale")).state),
             static_cast<int>(PropertyReadState::Value));
    auto setScale = provider.makeSet(selection, PropertyPath("transform.scale"), EditorValue::Array{2.0, 3.0, 4.0},
                                     PropertySetMode::Absolute);
    REQUIRE(setScale.ok());
    REQUIRE(commitOperation(target, transactions, setScale.value(), "property.scale").ok());
    CHECK_EQ(target.readTransform(ObjectId("one")).value().scaleY, 3.0);
    CHECK_EQ(target.readTransform(ObjectId("two")).value().scaleZ, 4.0);
    REQUIRE(transactions.undo().ok());
    CHECK_EQ(target.readTransform(ObjectId("one")).value().scaleY, 1.0);
    CHECK_EQ(target.readTransform(ObjectId("two")).value().scaleZ, 1.0);

    SceneTransformToolLogic transform;
    SceneTransformValue     distinct;
    distinct.x   = 9.0;
    auto moveOne = transform.plan(target, ObjectId("one"), distinct);
    REQUIRE(moveOne.ok());
    REQUIRE(commitOperation(target, transactions, moveOne.value(), "property.mixed").ok());
    CHECK_EQ(static_cast<int>(provider.read(selection, PropertyPath("transform.position")).state),
             static_cast<int>(PropertyReadState::Mixed));
}
}  // namespace test_editor_scene_target_v2

// Contract section: editor skeleton overlay
#include "PrivateTestScope.h"

#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/editing/SkeletonOverlay.h"
// editor_skeleton_overlay.cpp
namespace test_editor_skeleton_overlay {
using namespace eve::animation_editing;
using namespace eve::editing;

namespace {

const GizmoPrimitive* primitive(const GizmoSnapshot& snapshot, const std::string& id) {
    const auto found = std::find_if(snapshot.primitives.begin(), snapshot.primitives.end(),
                                    [&](const auto& value) { return value.id == id; });
    return found == snapshot.primitives.end() ? nullptr : &*found;
}

}  // namespace

TEST_CASE("editor.animation.skeleton_overlay_builds_lines_axes_constraints_retarget_and_mask") {
    SkeletonOverlayBone root;
    root.id       = StableId("root");
    root.name     = "root";
    root.position = {0.0, 0.0, 0.0};

    SkeletonOverlayBone spine;
    spine.id            = StableId("spine");
    spine.parent        = root.id;
    spine.name          = "spine";
    spine.position      = {0.0, 1.0, 0.0};
    spine.selected      = true;
    spine.constrained   = true;
    spine.minimumAngles = {-0.5, -0.25};
    spine.maximumAngles = {0.5, 0.75};
    spine.retarget      = SkeletonRetargetState::Matched;

    SkeletonOverlayBone hand;
    hand.id         = StableId("hand");
    hand.parent     = spine.id;
    hand.name       = "hand";
    hand.position   = {1.0, 1.0, 0.0};
    hand.retarget   = SkeletonRetargetState::Unmatched;
    hand.maskWeight = 0.0;

    auto overlay = SkeletonOverlayBuilder().build("avatar", 17, {root, spine, hand});
    REQUIRE_EQ(static_cast<int>(overlay.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(overlay.targetRevision, Revision{17});
    CHECK_EQ(overlay.primitives.size(), size_t{10});
    REQUIRE(primitive(overlay, "spine:bone"));
    CHECK_EQ(primitive(overlay, "spine:bone")->length, 1.0);
    REQUIRE(primitive(overlay, "spine:axis-x"));
    REQUIRE(primitive(overlay, "spine:constraint-yaw"));
    REQUIRE(primitive(overlay, "hand:joint"));
    CHECK(primitive(overlay, "hand:joint")->dashed);
    CHECK(primitive(overlay, "hand:joint")->color[0] < 0.3);
    CHECK(!primitive(overlay, "hand:axis-x"));
}

TEST_CASE("editor.animation.skeleton_overlay_rejects_cycles_missing_parents_and_budget") {
    SkeletonOverlayBone first;
    first.id     = StableId("first");
    first.parent = StableId("second");
    SkeletonOverlayBone second;
    second.id     = StableId("second");
    second.parent = StableId("first");
    CHECK_EQ(static_cast<int>(SkeletonOverlayBuilder().build("cycle", 1, {first, second}).status),
             static_cast<int>(EditorStatus::Failed));

    first.parent = StableId("missing");
    CHECK_EQ(static_cast<int>(SkeletonOverlayBuilder().build("missing", 1, {first}).status),
             static_cast<int>(EditorStatus::Failed));

    first.parent = {};
    SkeletonOverlayOptions options;
    options.maximumBones = 0;
    CHECK_EQ(static_cast<int>(SkeletonOverlayBuilder().build("budget", 1, {first}, options).status),
             static_cast<int>(EditorStatus::Failed));

    options.maximumBones = 1;
    first.maskWeight     = 1.5;
    CHECK_EQ(static_cast<int>(SkeletonOverlayBuilder().build("mask", 1, {first}, options).status),
             static_cast<int>(EditorStatus::Failed));
}

TEST_CASE("editor.animation.runtime_skeleton_adapter_uses_world_pose_and_stable_names") {
    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("root");
    const int                    hand = skeleton.addBone("hand", root);
    eve::animation::AnimPose     pose(skeleton.getBoneCount());
    skeleton.applyBindPose(&pose);
    pose.setLocalPosition(root, 2.f, 3.f, 4.f);
    pose.setLocalPosition(hand, 0.f, 2.f, 0.f);

    auto overlay = AnimationSkeletonOverlayAdapter().build(&skeleton, &pose, "runtime-avatar", 9, "hand",
                                                           {{"root", "hips"}, {"hand", ""}}, {{"hand", 0.5}});
    REQUIRE_EQ(static_cast<int>(overlay.status), static_cast<int>(EditorStatus::Applied));
    REQUIRE(primitive(overlay, "root:joint"));
    CHECK_EQ(primitive(overlay, "root:joint")->position[0], 2.0);
    REQUIRE(primitive(overlay, "hand:joint"));
    CHECK_EQ(primitive(overlay, "hand:joint")->position[1], 5.0);
    CHECK(primitive(overlay, "hand:joint")->dashed);
    REQUIRE(primitive(overlay, "hand:axis-z"));
}
}  // namespace test_editor_skeleton_overlay
