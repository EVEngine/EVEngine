#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "gpuagents/EffectBackend.h"
#include "gpuagents/GpuAgentWorld.h"
#include "gpuagents/LifeFieldMaterialBinding.h"
#include "gpuagents/SurfaceCapture.h"
#include "gpuagents/editing/GpuAgentsDocument.h"
#include "gpuagents/editor/GpuAgentsPreview.h"

#include <cmath>
#include <vector>

using namespace eve::gpuagents;
using namespace eve::gpuagents_editing;
using namespace eve::editing;

namespace {

SelectionSnapshot selection(const char* target) {
    SelectionSnapshot value;
    value.items.push_back({SelectionDomain::Asset, TargetId(target), StableId("effect"), "gpuagents.effect"});
    return value;
}

}  // namespace

TEST_CASE("gpuagents.surface_capture_builds_height_and_normals") {
    // Ramp triangle covering the domain center.
    std::vector<glm::vec3> positions = {
        {-4.f, 0.f, -4.f},
        {4.f, 2.f, -4.f},
        {0.f, 1.f, 4.f},
    };
    std::vector<std::uint32_t> indices = {0, 1, 2};
    SurfaceField               field;
    REQUIRE(SurfaceCapture::captureFromTriangles(field, positions, indices, glm::vec3(-8.f, 0.f, -8.f), 16.f, 33).ok());
    CHECK_EQ(field.resolution, 33);
    CHECK(field.sampleHeight(0.f, 0.f) > 0.2f);
    const glm::vec3 n = field.sampleNormal(0.f, 0.f);
    CHECK(n.y > 0.5f);
}

TEST_CASE("gpuagents.life_field_material_binding_packs_rgba") {
    SurfaceField field;
    field.initFlat(glm::vec3(-4.f, 0.f, -4.f), 8.f, 16, 0.f);
    field.lifeField[static_cast<size_t>(8 + 16 * 8)] = glm::vec4(1.f, 0.5f, 0.25f, 1.f);
    LifeFieldMaterialBinding binding;
    REQUIRE(binding.syncFrom(field).ok());
    CHECK_EQ(binding.uniforms().resolution, 16);
    CHECK_EQ(binding.lifeFieldPixels().size(), static_cast<size_t>(16 * 16 * 4));
    CHECK_EQ(binding.surfaceDataPixels().size(), static_cast<size_t>(16 * 16 * 4));
    const size_t p = static_cast<size_t>(8 + 16 * 8) * 4u;
    CHECK(binding.lifeFieldPixels()[p] >= 250);  // trail ~1
}

TEST_CASE("editor.gpuagents_document_schema_set_undo_and_runtime_apply") {
    GpuAgentsDocumentTarget target("school");
    const auto              selected = selection("school");
    CHECK(target.schema(selected).properties.size() >= 20u);
    auto setKind =
        target.makeSet(selected, PropertyPath("kind"), std::string("LifeNetwork"), PropertySetMode::Absolute);
    REQUIRE(setKind.ok());
    REQUIRE(target.applyDomainOperation(setKind.value()).ok());
    CHECK_EQ(target.settings().kind, std::string("LifeNetwork"));
    auto setDeposit = target.makeSet(selected, PropertyPath("depositStrength"), 0.9, PropertySetMode::Absolute);
    REQUIRE(setDeposit.ok());
    REQUIRE(target.applyDomainOperation(setDeposit.value()).ok());
    CHECK_EQ(target.settings().depositStrength, 0.9);
    DomainOperation undo = setDeposit.value();
    undo.payload         = setDeposit.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(target.settings().depositStrength, 0.35);

    GpuAgentWorld           world;
    EffectBackend           backend;
    GpuAgentsRuntimeApplier applier;
    REQUIRE(applier.apply(target, &backend, &world).ok());
    CHECK_EQ(static_cast<int>(backend.kind()), static_cast<int>(EffectKind::LifeNetwork));
    CHECK_EQ(world.surface().resolution, static_cast<int>(target.settings().fieldResolution));
}

TEST_CASE("editor.gpuagents_preview_steps_life_network") {
    GpuAgentsDocumentTarget target("life");
    const auto              selected = selection("life");
    auto                    setKind =
        target.makeSet(selected, PropertyPath("kind"), std::string("LifeNetwork"), PropertySetMode::Absolute);
    REQUIRE(setKind.ok());
    REQUIRE(target.applyDomainOperation(setKind.value()).ok());
    auto setRes = target.makeSet(selected, PropertyPath("fieldResolution"), static_cast<std::int64_t>(32),
                                 PropertySetMode::Absolute);
    REQUIRE(setRes.ok());
    REQUIRE(target.applyDomainOperation(setRes.value()).ok());

    eve::gpuagents_editor::GpuAgentsPreviewService preview;
    eve::gpuagents_editor::GpuAgentsPreviewRequest request;
    request.agentCount = 16;
    request.seconds    = 0.5;
    request.fixedStep  = 1.0 / 60.0;
    auto snap          = preview.build(target, request);
    CHECK_EQ(static_cast<int>(snap.status), static_cast<int>(EditorStatus::Applied));
    CHECK(snap.agents.size() == 16u);
    CHECK(snap.simulatedSeconds > 0.4);
    CHECK(snap.lifeTrailEnergy > 0.f);
}
