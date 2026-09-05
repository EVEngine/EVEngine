#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editing/EditingCommandTypes.h"
#include "editing/EditingAuthority.h"
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
#include "editing/EditableTarget.h"
#include "definitions_editing/DefinitionTarget.h"
#include "definitions_editor/EditorDefinitionTarget.h"
#include "editor/EditorAuthority.h"
#include "avatar_editor/EditorAvatarTarget.h"
#include "biome_editor/EditorBiomeTarget.h"
#include "camera_editor/EditorCameraTarget.h"
#include "animation_editor/EditorAnimationClip.h"
#include "editor/EditorCommandTypes.h"
#include "material_editor/EditorGraph.h"
#include "fluids_editor/EditorFluidTarget.h"
#include "hd2d_editor/EditorHd2dTarget.h"
#include "housegen_editor/EditorHouseGenTarget.h"
#include "input_editor/EditorInputMapTarget.h"
#include "lighting_editor/EditorLightingTarget.h"
#include "editor/EditorIds.h"
#include "map_editor/EditorMapDocument.h"
#include "material_editor/EditorMaterialTarget.h"
#include "particles_editor/EditorParticleGraph.h"
#include "editor/EditorProperty.h"
#include "editor/EditorProtocol.h"
#include "physics_editor/EditorPhysicsAsset.h"
#include "physics_editor/EditorPhysicsTarget.h"
#include "editor/EditorResult.h"
#include "editor/EditorSelection.h"
#include "scene_editor/EditorSceneTarget.h"
#include "fluids_editor/EditorSurfaceFluidTarget.h"
#include "editor/EditorTarget.h"
#include "editor/EditorTaskService.h"
#include "ui_editor/EditorUiDocumentTarget.h"
#include "ui_editor/EditorUiThemeTarget.h"
#include "editor/EditorValue.h"
#include "virtualgeometry_editor/EditorVirtualGeometryTarget.h"
#include "editor/EditorVolumeTarget.h"
#include "voxel_editor/EditorVoxelCatalogTarget.h"
#include "voxel_editor/EditorVoxelPaletteTarget.h"
#include "physics_editing/PhysicsColliderAsset.h"
#include "physics_editing/PhysicsTarget.h"
#include "map_editing/MapDocument.h"
#include "material_editing/MaterialGraph.h"
#include "material_editing/MaterialTarget.h"
#include "particles_editing/ParticleGraph.h"
#include "procgen_editing/TextureRecipeTarget.h"
#include "procgen_editor/EditorTextureRecipeTarget.h"
#include "animation_editing/AnimationClip.h"
#include "avatar_editing/AvatarTarget.h"
#include "biome_editing/BiomeTarget.h"
#include "camera_editing/CameraTarget.h"
#include "building_editing/BuildingTarget.h"
#include "crowd_editing/CrowdDocument.h"
#include "localization_editing/LocalizationDocument.h"
#include "social_editing/SocialDocument.h"
#include "building_editor/EditorBuildingTarget.h"
#include "crowd_editor/EditorCrowdDocument.h"
#include "localization_editor/EditorLocalization.h"
#include "social_editor/EditorSocialDocument.h"
#include "scene_editing/SceneTarget.h"
#include "sceneloader_editing/SceneImportTarget.h"
#include "sceneloader_editor/EditorSceneImportTarget.h"
#include "ui_editing/UiDocument.h"
#include "input_editing/InputMapTarget.h"
#include "hd2d_editing/Hd2dTarget.h"
#include "housegen_editing/HouseGenTarget.h"
#include "fluids_editing/FluidTarget.h"
#include "fluids_editing/SurfaceFluidTarget.h"
#include "lighting_editing/LightingTarget.h"
#include "virtualgeometry_editing/VirtualGeometryTarget.h"
#include "voxel_editing/VoxelPaletteTarget.h"
#include "voxel_editing/VoxelCatalog.h"
#include "voxel_editing/VoxelWorldTarget.h"
#include "network_editing/NetworkTelemetry.h"
#include "profiler_editing/ProfilerModel.h"
#include "queue_editing/QueueInspector.h"
#include "graphics_editing/OffscreenPreview.h"
#include "domain_gizmo_editing/DomainGizmoPreview.h"
#include "network_editor/EditorNetworkTelemetry.h"
#include "profiler_editor/EditorProfiler.h"
#include "queue_editor/EditorQueueInspector.h"
#include "graphics_editor/EditorOffscreenPreview.h"
#include "domain_gizmo_editor/EditorGizmoPreview.h"

#include <string>
#include <type_traits>

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
static_assert(
    std::is_same_v<eve::editor::IDomainOperationTargetStaging, eve::editing::IDomainOperationTargetStaging>);
static_assert(std::is_same_v<eve::editor::PropertySchema, eve::editing::PropertySchema>);
static_assert(std::is_same_v<eve::editor::IPropertyProvider, eve::editing::IPropertyProvider>);
static_assert(std::is_same_v<eve::editor::IEditAuthority, eve::editing::IEditAuthority>);
static_assert(std::is_same_v<eve::editor::LocalWorldAuthority, eve::editing::LocalWorldAuthority>);
static_assert(std::is_same_v<eve::editor::GraphDocument, eve::editing::GraphDocument>);
static_assert(std::is_same_v<eve::editor::IGraphDomainProvider, eve::editing::IGraphDomainProvider>);
static_assert(std::is_same_v<eve::editor::EditorTaskService, eve::editing::TaskService>);
static_assert(std::is_same_v<eve::editor::DefinitionDocument,
                             eve::definitions_editing::DefinitionDocument>);
static_assert(std::is_same_v<eve::editor::MaterialGraphDomain, eve::material_editing::MaterialGraphDomain>);
static_assert(std::is_same_v<eve::editor::MaterialDocumentTarget, eve::material_editing::MaterialDocumentTarget>);
static_assert(std::is_same_v<eve::editor::PhysicsColliderTarget, eve::physics_editing::PhysicsColliderTarget>);
static_assert(
    std::is_same_v<eve::editor::IPhysicsColliderAssetResolver, eve::physics_editing::IPhysicsColliderAssetResolver>);
static_assert(std::is_same_v<eve::editor::SceneDocumentTarget, eve::scene_editing::SceneDocumentTarget>);
static_assert(std::is_same_v<eve::editor::SceneImportTarget,
                             eve::sceneloader_editing::SceneImportTarget>);
static_assert(std::is_same_v<eve::editor::MapDocumentTarget, eve::map_editing::MapDocumentTarget>);
static_assert(std::is_same_v<eve::editor::AnimationClipDocumentTarget,
                             eve::animation_editing::AnimationClipDocumentTarget>);
static_assert(std::is_same_v<eve::editor::AvatarDocumentTarget, eve::avatar_editing::AvatarDocumentTarget>);
static_assert(std::is_same_v<eve::editor::BiomeDocumentTarget, eve::biome_editing::BiomeDocumentTarget>);
static_assert(std::is_same_v<eve::editor::CameraDocumentTarget, eve::camera_editing::CameraDocumentTarget>);
static_assert(std::is_same_v<eve::editor::BuildingPlacementTarget,
                             eve::building_editing::BuildingPlacementTarget>);
static_assert(std::is_same_v<eve::editor::CrowdDocumentTarget,
                             eve::crowd_editing::CrowdDocumentTarget>);
static_assert(std::is_same_v<eve::editor::LocalizationDocument,
                             eve::localization_editing::LocalizationDocument>);
static_assert(std::is_same_v<eve::editor::SocialDocumentTarget,
                             eve::social_editing::SocialDocumentTarget>);
static_assert(std::is_same_v<eve::editor::NetworkTelemetryModel,
                             eve::network_editing::NetworkTelemetryModel>);
static_assert(std::is_same_v<eve::editor::EditorProfilerModel,
                             eve::profiler_editing::EditorProfilerModel>);
static_assert(std::is_same_v<eve::editor::RuntimeQueueInspector,
                             eve::queue_editing::RuntimeQueueInspector>);
static_assert(std::is_same_v<eve::editor::GraphicsOffscreenPreviewService,
                             eve::graphics_editing::GraphicsOffscreenPreviewService>);
static_assert(std::is_same_v<eve::editor::EditorGizmoPreviewBuilder,
                             eve::domain_gizmo_editing::EditorGizmoPreviewBuilder>);
static_assert(std::is_same_v<eve::editor::ParticleGraphDomain, eve::particles_editing::ParticleGraphDomain>);
static_assert(std::is_same_v<eve::editor::TextureRecipeTarget,
                             eve::procgen_editing::TextureRecipeTarget>);
static_assert(std::is_same_v<eve::editor::UiDocumentTarget, eve::ui_editing::UiDocumentTarget>);
static_assert(std::is_same_v<eve::editor::UiThemeCatalogTarget, eve::ui_editing::UiThemeCatalogTarget>);
static_assert(std::is_same_v<eve::editor::InputMapTarget, eve::input_editing::InputMapTarget>);
static_assert(std::is_same_v<eve::editor::Hd2dDocumentTarget, eve::hd2d_editing::Hd2dDocumentTarget>);
static_assert(std::is_same_v<eve::editor::HouseGenDocumentTarget,
                             eve::housegen_editing::HouseGenDocumentTarget>);
static_assert(std::is_same_v<eve::editor::FluidSimulationTarget, eve::fluids_editing::FluidSimulationTarget>);
static_assert(std::is_same_v<eve::editor::SurfaceFluidTarget, eve::fluids_editing::SurfaceFluidTarget>);
static_assert(std::is_same_v<eve::editor::Light3DDocumentTarget,
                             eve::lighting_editing::Light3DDocumentTarget>);
static_assert(std::is_same_v<eve::editor::VirtualGeometryDocumentTarget,
                             eve::virtualgeometry_editing::VirtualGeometryDocumentTarget>);
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
    CHECK_EQ(eve::editing::diagnosticRule(rejected.diagnostics().front()).value(),
             std::string("authoring.invalid"));
    rejected.ignore();
}
