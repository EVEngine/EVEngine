#include "fluids_editor/EditorSurfaceFluidPreview.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

namespace {
SurfaceFluidPreviewRequest request(const SurfaceFluidTarget& target, double seconds) {
    SurfaceFluidPreviewRequest value;
    value.previewId = StableId("surface-fluid");
    value.documentRevision = target.revision();
    value.seconds = seconds;
    value.fixedStep = 1.0 / 60.0;
    value.positions = {{{0.0, 0.0, 0.0}}, {{2.0, -1.0, 0.0}}, {{0.0, 0.0, 2.0}}};
    value.indices = {0, 1, 2};
    value.uvs = {{{0.0, 0.0}}, {{1.0, 0.0}}, {{0.0, 1.0}}};
    SurfaceFluidPreviewSeed seed;
    seed.barycentric = {0.8, 0.1, 0.1};
    seed.volume = 0.02;
    value.seeds.push_back(seed);
    return value;
}
}

TEST_CASE("editor.surface_fluid.preview_replays_forward_and_backward_scrub_deterministically") {
    SurfaceFluidTarget target("waterfall"); SurfaceFluidPreviewService previews;
    const auto later = previews.build(target, request(target, 0.8));
    REQUIRE_EQ(static_cast<int>(later.status), static_cast<int>(EditorStatus::Applied));
    REQUIRE(!later.droplets.empty());
    CHECK_EQ(later.vertexWetness.size(), static_cast<std::size_t>(3));
    const auto earlier = previews.build(target, request(target, 0.2));
    REQUIRE_EQ(static_cast<int>(earlier.status), static_cast<int>(EditorStatus::Applied));
    const auto replayed = previews.build(target, request(target, 0.8));
    CHECK(replayed.droplets == later.droplets);
    CHECK(replayed.vertexWetness == later.vertexWetness);
    CHECK_EQ(replayed.simulatedSeconds, 0.8);
}

TEST_CASE("editor.surface_fluid.preview_rejects_stale_invalid_and_unbounded_requests") {
    SurfaceFluidTarget target("waterfall"); SurfaceFluidPreviewService previews;
    auto stale = request(target, 0.1); stale.documentRevision++;
    CHECK_EQ(static_cast<int>(previews.build(target, stale).status),
             static_cast<int>(EditorStatus::Conflict));
    auto vertices = request(target, 0.1); vertices.maximumVertices = 2;
    CHECK_EQ(static_cast<int>(previews.build(target, vertices).status),
             static_cast<int>(EditorStatus::Rejected));
    auto steps = request(target, 1.0); steps.fixedStep = 1.0e-9;
    CHECK_EQ(static_cast<int>(previews.build(target, steps).status),
             static_cast<int>(EditorStatus::Rejected));
    auto seed = request(target, 0.1); seed.seeds[0].barycentric = {0.8, 0.8, 0.0};
    CHECK_EQ(static_cast<int>(previews.build(target, seed).status),
             static_cast<int>(EditorStatus::Rejected));
}
