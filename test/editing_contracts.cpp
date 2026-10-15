#include <zeroerr/unittest.h>
#include <string>
#include <type_traits>
#include <vector>
#include "animation/editing/AnimationClip.h"
#include "animation/editor/EditorAnimationClip.h"
#include "avatar/editing/AvatarTarget.h"
#include "avatar/editor/EditorAvatarTarget.h"
#include "building/editing/BuildingTarget.h"
#include "building/editor/EditorBuildingTarget.h"
#include "camera/editing/CameraTarget.h"
#include "crowd/editing/CrowdDocument.h"
#include "definitions/editing/DefinitionTarget.h"
#include "editing/EditableTarget.h"
#include "editing/EditingAuthority.h"
#include "editing/EditingCommandTypes.h"
#include "editing/EditingExtension.h"
#include "editing/EditingGraph.h"
#include "editing/EditingIds.h"
#include "editing/EditingProperty.h"
#include "editing/EditingProtocol.h"
#include "editing/EditingResult.h"
#include "editing/EditingSelection.h"
#include "editing/EditingTargetOperations.h"
#include "editing/EditingTaskService.h"
#include "editing/EditingValue.h"
#include "editing/EditingVolume.h"
#include "editor/Editor.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorCommandTypes.h"
#include "editor/EditorIds.h"
#include "editor/EditorProperty.h"
#include "editor/EditorProtocol.h"
#include "editor/EditorResult.h"
#include "editor/EditorSelection.h"
#include "editor/EditorTarget.h"
#include "editor/EditorTaskService.h"
#include "editor/EditorValue.h"
#include "editor/EditorVolumeTarget.h"
#include "editor/gizmo/editing/DomainGizmoPreview.h"
#include "editor/gizmo/editor/EditorGizmoPreview.h"
#include "editor/input/editing/InputMapTarget.h"
#include "editor/input/editor/EditorInputMapTarget.h"
#include "editor/queue/editing/QueueInspector.h"
#include "editor/queue/editor/EditorQueueInspector.h"
#include "fluids/editing/FluidTarget.h"
#include "fluids/editing/SurfaceFluidTarget.h"
#include "fluids/editing/VolumeFluidTarget.h"
#include "fluids/editor/EditorFluidTarget.h"
#include "fluids/editor/EditorSurfaceFluidTarget.h"
#include "fluids/editor/EditorVolumeFluidTarget.h"
#include "graphics/editing/OffscreenPreview.h"
#include "graphics/editor/EditorOffscreenPreview.h"
#include "graphics/lighting/editing/LightingTarget.h"
#include "graphics/lighting/editor/EditorLightingTarget.h"
#include "graphics/material/editing/MaterialGraph.h"
#include "graphics/material/editing/MaterialTarget.h"
#include "graphics/material/editor/EditorGraph.h"
#include "graphics/material/editor/EditorMaterialTarget.h"
#include "hd2d/editing/Hd2dTarget.h"
#include "i18n/editing/LocalizationDocument.h"
#include "map/editing/MapDocument.h"
#include "network/editing/NetworkTelemetry.h"
#include "particles/editing/ParticleGraph.h"
#include "physics/editing/PhysicsColliderAsset.h"
#include "physics/editing/PhysicsTarget.h"
#include "physics/editor/EditorPhysicsAsset.h"
#include "physics/editor/EditorPhysicsTarget.h"
#include "procgen/biome/editing/BiomeTarget.h"
#include "procgen/biome/editor/EditorBiomeTarget.h"
#include "procgen/editing/HouseGenTarget.h"
#include "procgen/editing/TextureRecipeTarget.h"
#include "procgen/editor/EditorHouseGenTarget.h"
#include "procgen/editor/EditorTextureRecipeTarget.h"
#include "profiler/editing/ProfilerModel.h"
#include "scene/editing/SceneTarget.h"
#include "scene/editor/EditorSceneTarget.h"
#include "scene/loader/editing/SceneImportTarget.h"
#include "scene/loader/editor/EditorSceneImportTarget.h"
#include "social/editing/SocialDocument.h"
#include "ui/editing/UiDocument.h"
#include "ui/editor/EditorUiDocumentTarget.h"
#include "ui/editor/EditorUiThemeTarget.h"
#include "virtualgeometry/editing/VirtualGeometryTarget.h"
#include "voxel/editing/VoxelCatalog.h"
#include "voxel/editing/VoxelPaletteTarget.h"
#include "voxel/editing/VoxelWorldTarget.h"
#include "voxel/editor/EditorVoxelCatalogTarget.h"
#include "voxel/editor/EditorVoxelPaletteTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editing_contracts.cpp
static_assert(!std::is_convertible_v<eve::editing::CommandId, eve::editing::ToolId>);
static_assert(std::is_same_v<eve::editor::CommandId, eve::editing::CommandId>);
static_assert(std::is_same_v<eve::editor::EditorValue, eve::editing::Value>);
static_assert(std::is_same_v<eve::editor::EditorStatus, eve::editing::Status>);
static_assert(std::is_same_v<eve::editor::CommandSource, eve::editing::CommandSource>);
static_assert(std::is_same_v<eve::editor::CommandRequest, eve::editing::CommandRequest>);
static_assert(std::is_same_v<eve::editor::TransactionReceipt, eve::editing::TransactionReceipt>);
static_assert(std::is_same_v<eve::editor::SelectionSnapshot, eve::editing::SelectionSnapshot>);
static_assert(std::is_same_v<eve::editor::IEditableTarget, eve::editing::IEditableTarget>);
static_assert(std::is_same_v<eve::editor::IIntVolumeTarget, eve::editing::IIntVolumeTarget>);
static_assert(std::is_same_v<eve::editor::IDomainOperationTarget, eve::editing::IDomainOperationTarget>);
static_assert(std::is_same_v<eve::editor::IDomainOperationTargetStaging, eve::editing::IDomainOperationTargetStaging>);
static_assert(std::is_same_v<eve::editor::PropertySchema, eve::editing::PropertySchema>);
static_assert(std::is_same_v<eve::editor::IPropertyProvider, eve::editing::IPropertyProvider>);
static_assert(std::is_same_v<eve::editor::IEditAuthority, eve::editing::IEditAuthority>);
static_assert(std::is_same_v<eve::editor::LocalWorldAuthority, eve::editing::LocalWorldAuthority>);
static_assert(std::is_same_v<eve::editor::GraphDocument, eve::editing::GraphDocument>);
static_assert(std::is_same_v<eve::editor::IGraphDomainProvider, eve::editing::IGraphDomainProvider>);
static_assert(std::is_same_v<eve::editor::EditorTaskService, eve::editing::TaskService>);
static_assert(std::is_same_v<eve::editor::MaterialGraphDomain, eve::material_editing::MaterialGraphDomain>);
static_assert(std::is_same_v<eve::editor::MaterialDocumentTarget, eve::material_editing::MaterialDocumentTarget>);
static_assert(std::is_same_v<eve::editor::PhysicsColliderTarget, eve::physics_editing::PhysicsColliderTarget>);
static_assert(
    std::is_same_v<eve::editor::IPhysicsColliderAssetResolver, eve::physics_editing::IPhysicsColliderAssetResolver>);
static_assert(std::is_same_v<eve::editor::SceneDocumentTarget, eve::scene_editing::SceneDocumentTarget>);
static_assert(std::is_same_v<eve::editor::SceneImportTarget, eve::sceneloader_editing::SceneImportTarget>);
static_assert(
    std::is_same_v<eve::editor::AnimationClipDocumentTarget, eve::animation_editing::AnimationClipDocumentTarget>);
static_assert(std::is_same_v<eve::editor::AvatarDocumentTarget, eve::avatar_editing::AvatarDocumentTarget>);
static_assert(std::is_same_v<eve::editor::BiomeDocumentTarget, eve::biome_editing::BiomeDocumentTarget>);
static_assert(std::is_same_v<eve::editor::BuildingPlacementTarget, eve::building_editing::BuildingPlacementTarget>);
static_assert(std::is_same_v<eve::editor::RuntimeQueueInspector, eve::queue_editing::RuntimeQueueInspector>);
static_assert(std::is_same_v<eve::editor::GraphicsOffscreenPreviewService,
                             eve::graphics_editing::GraphicsOffscreenPreviewService>);
static_assert(
    std::is_same_v<eve::editor::EditorGizmoPreviewBuilder, eve::domain_gizmo_editing::EditorGizmoPreviewBuilder>);
static_assert(std::is_same_v<eve::editor::TextureRecipeTarget, eve::procgen_editing::TextureRecipeTarget>);
static_assert(std::is_same_v<eve::editor::UiDocumentTarget, eve::ui_editing::UiDocumentTarget>);
static_assert(std::is_same_v<eve::editor::UiThemeCatalogTarget, eve::ui_editing::UiThemeCatalogTarget>);
static_assert(std::is_same_v<eve::editor::InputMapTarget, eve::input_editing::InputMapTarget>);
static_assert(std::is_same_v<eve::editor::HouseGenDocumentTarget, eve::housegen_editing::HouseGenDocumentTarget>);
static_assert(std::is_same_v<eve::editor::FluidSimulationTarget, eve::fluids_editing::FluidSimulationTarget>);
static_assert(std::is_same_v<eve::editor::SurfaceFluidTarget, eve::fluids_editing::SurfaceFluidTarget>);
static_assert(std::is_same_v<eve::editor::VolumeFluidTarget, eve::fluids_editing::VolumeFluidTarget>);
static_assert(std::is_same_v<eve::editor::Light3DDocumentTarget, eve::lighting_editing::Light3DDocumentTarget>);
static_assert(std::is_same_v<eve::editor::VoxelPaletteTarget, eve::voxel_editing::VoxelPaletteTarget>);
static_assert(std::is_same_v<eve::editor::VoxelCatalogTarget, eve::voxel_editing::VoxelCatalogTarget>);
static_assert(std::is_same_v<eve::editor::VoxelModelValue, eve::voxel_editing::VoxelModelValue>);
static_assert(!std::is_copy_constructible_v<eve::editing::Result<int>>);
static_assert(!std::is_copy_constructible_v<eve::editing::Result<void>>);

TEST_CASE("editing.contracts.ids_values_and_editor_compatibility") {
    using namespace eve::editing;

    const CommandId command("scene.object.create");
    CHECK_EQ(command.value(), std::string("scene.object.create"));
    CHECK_EQ(eve::editor::CommandId("scene.object.create"), command);

    Value::Object root;
    root["name"]     = "Station";
    root["position"] = Value::Array{1.0, 2.0, 3.0};
    const Value value(std::move(root));
    CHECK_EQ(static_cast<int>(value.type()), static_cast<int>(Value::Type::Object));
    CHECK(value.isWithinLimits(4, 16, 128));
    CHECK(!value.isWithinLimits(1, 16, 128));
}

TEST_CASE("editing.contracts.property_path_is_textual") {
    using namespace eve::editing;
    const PropertyPath left("layout.size");
    const PropertyPath same("layout.size");
    const PropertyPath right("layout.position");
    CHECK(left == same);
    CHECK(left != right);
    CHECK_EQ(left.value(), std::string("layout.size"));
}

TEST_CASE("editing.contracts.structured_result") {
    using namespace eve::editing;

    const auto accepted = eve::editing::applied<Value>(Value("ready"));
    CHECK(accepted.ok());
    CHECK_EQ(*accepted.value().getIf<std::string>(), std::string("ready"));

    const auto rejected = eve::editing::failed<void>(Status::Rejected, RuleId("authoring.invalid"), "invalid input");
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.diagnostics().size(), size_t{1});
    CHECK_EQ(eve::editing::diagnosticRule(rejected.diagnostics().front()).value(), std::string("authoring.invalid"));
    rejected.ignore();
}

// editing_extension_lifecycle.cpp
using namespace eve::editing;

namespace {
class Provider final : public IEditingExtensionProvider {
public:
    Result<void> activate() override {
        ++activated;
        return eve::editing::applied<void>();
    }
    Result<void> beginUnload() override {
        ++stopping;
        return rejectUnload ? eve::editing::failed<void>(Status::Failed, RuleId("test.unload"), "injected")
                            : eve::editing::applied<void>();
    }
    void  deactivate() noexcept override { ++deactivated; }
    void* query(const CapabilityId& capability) noexcept override {
        return capability == CapabilityId("test.capability") ? this : nullptr;
    }
    bool rejectUnload = false;
    int  activated    = 0;
    int  stopping     = 0;
    int  deactivated  = 0;
};
}  // namespace

TEST_CASE("editing.extension.provider_absent_unload_and_stale_handle_are_explicit") {
    ExtensionProviderRegistry registry;
    auto                      absent = registry.acquire("missing");
    CHECK_EQ(static_cast<int>(absent.code()), static_cast<int>(Status::Unsupported));

    auto                provider = std::make_shared<Provider>();
    ExtensionDescriptor descriptor;
    descriptor.id           = "test.provider";
    descriptor.capabilities = {CapabilityId("test.capability")};
    auto registered         = registry.registerProvider(descriptor, provider);
    REQUIRE(registered.ok());
    CHECK_EQ(provider->activated, 1);

    auto lease = registry.acquire(registered.value());
    REQUIRE(lease.ok());
    CHECK(lease.value().query(CapabilityId("test.capability")) == provider.get());
    CHECK(registry.unload(registered.value()).ok());
    CHECK_EQ(provider->stopping, 1);
    CHECK_EQ(provider->deactivated, 1);
    auto stale = registry.acquire(registered.value());
    CHECK_EQ(static_cast<int>(stale.code()), static_cast<int>(Status::Conflict));
    CHECK(lease.value().query(CapabilityId("test.capability")) == provider.get());
}

TEST_CASE("editing.extension.failed_unload_preserves_current_generation") {
    ExtensionProviderRegistry registry;
    auto                      provider = std::make_shared<Provider>();
    ExtensionDescriptor       descriptor;
    descriptor.id   = "test.rejecting";
    auto registered = registry.registerProvider(descriptor, provider);
    REQUIRE(registered.ok());
    provider->rejectUnload = true;
    CHECK(!registry.unload(registered.value()).ok());
    CHECK(registry.acquire(registered.value()).ok());
    CHECK_EQ(provider->deactivated, 0);
    provider->rejectUnload = false;
    CHECK(registry.unload(registered.value()).ok());
}

TEST_CASE("editor.composition.discovers_extension_provider_without_domain_type_list") {
    eve::editor::Editor host;
    auto                provider = std::make_shared<Provider>();
    ExtensionDescriptor descriptor;
    descriptor.id           = "test.open-domain-provider";
    descriptor.capabilities = {CapabilityId("test.capability")};

    auto registered = host.extensionProviders().registerProvider(std::move(descriptor), provider);
    REQUIRE(registered.ok());
    const auto descriptors = host.extensionProviders().descriptors();
    REQUIRE_EQ(descriptors.size(), static_cast<std::size_t>(1));
    CHECK_EQ(descriptors.front().id, std::string("test.open-domain-provider"));
    auto lease = host.extensionProviders().acquire(registered.value());
    REQUIRE(lease.ok());
    CHECK(lease.value().query(CapabilityId("test.capability")) == provider.get());
}

// editing_id_instances.cpp
static_assert(std::is_aggregate_v<eve::detail::UuidIdState>);
static_assert(std::is_nothrow_move_constructible_v<eve::editing::RuleId>);
static_assert(std::is_nothrow_move_assignable_v<eve::editing::RuleId>);
static_assert(std::is_nothrow_destructible_v<eve::editing::RuleId>);
TEST_CASE("editing.idInstances.owningLifecycleAndProjection") {
    eve::editing::RuleId original(std::string(4096, 'a'));
    auto                 copied = original;
    copied                      = eve::editing::RuleId("changed");
    CHECK(original.value().size() == 4096u);
    CHECK(copied.value() == "changed");
    auto moved = std::move(copied);
    copied     = eve::editing::RuleId("reused");
    eve::editing::RuleId assigned;
    assigned = original;
    assigned = std::move(moved);
    CHECK(assigned.value() == "changed");
    CHECK(copied.value() == "reused");
    auto parsed = eve::editing::RuleId::parse(original.value());
    CHECK(parsed.has_value());
    CHECK(parsed->uuid() == original.uuid());
    auto canonical = eve::editing::RuleId::fromUuid(original.uuid());
    CHECK(canonical.isCanonicalUuid());
    CHECK(canonical.canonicalFormat() == original.canonicalFormat());
    struct CustomTag;
    eve::editing::StrongId<CustomTag> custom("open-tag");
    CHECK(custom.value() == "open-tag");
}

TEST_CASE("editing.idInstances.distinctDomainTagsKeepProjection") {
    eve::editing::RuleId       rule("shared-spelling");
    eve::editing::TargetId     target("shared-spelling");
    eve::editing::StableId     stable("shared-spelling");
    eve::editing::ObjectId     object("shared-spelling");
    eve::editing::CapabilityId capability("shared-spelling");
    CHECK(rule.canonicalFormat() == target.canonicalFormat());
    CHECK(stable.canonicalFormat() == object.canonicalFormat());
    CHECK(capability.canonicalFormat() == rule.canonicalFormat());
    static_assert(!std::is_convertible_v<eve::editing::RuleId, eve::editing::TargetId>);
    static_assert(!std::is_convertible_v<eve::editing::StableId, eve::editing::ObjectId>);
}

TEST_CASE("editing.idInstances.capabilityVectorOwnsIndependentEntries") {
    using Id = eve::editing::CapabilityId;
    std::vector<Id> original;
    for (int i = 0; i < 64; ++i) original.emplace_back(std::string(4096, 'a') + std::to_string(i));
    auto       copied   = original;
    const auto expected = copied.front().value();
    original.front()    = Id("changed");
    CHECK(copied.front().value() == expected);
    auto moved = std::move(copied);
    copied.clear();
    copied.emplace_back("reused");
    std::vector<Id> assigned;
    assigned = original;
    CHECK(assigned.front().value() == "changed");
    assigned = std::move(moved);
    CHECK(assigned.front().value() == expected);
    CHECK(copied.front().value() == "reused");
    assigned.reserve(512);
    assigned.erase(assigned.begin());
    CHECK(assigned.size() == 63u);
    CHECK(assigned.front().value() == std::string(4096, 'a') + "1");
    CHECK(assigned.front().canonicalFormat() == original[1].canonicalFormat());
}

TEST_CASE("editing.idInstances.selectionVectorKeepsIndependentIdentityAndText") {
    using Item = eve::editing::SelectionItem;
    static_assert(std::is_aggregate_v<Item>);
    std::vector<Item> source;
    for (int i = 0; i < 64; ++i)
        source.push_back({eve::editing::SelectionDomain::Scene, eve::editing::TargetId("target"),
                          eve::editing::StableId(std::to_string(i)), std::string(4096, 'a')});
    auto copy           = source;
    source.front().type = "changed";
    CHECK(copy.front().type.size() == 4096u);
    auto moved = std::move(copy);
    copy.clear();
    copy.push_back({});
    CHECK(copy.size() == 1u);
    std::vector<Item> assigned;
    assigned = source;
    CHECK(assigned.front().type == "changed");
    assigned = std::move(moved);
    CHECK(assigned.front().type.size() == 4096u);
    assigned.reserve(512);
    assigned.erase(assigned.begin());
    CHECK(assigned.size() == 63u);
    CHECK(assigned.front().item.value() == "1");
    CHECK(assigned.front().target.canonicalFormat() == source[1].target.canonicalFormat());
    CHECK(static_cast<bool>(assigned.front().domain == eve::editing::SelectionDomain::Scene));
}
