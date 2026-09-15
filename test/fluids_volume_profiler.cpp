#include "common/Profile.h"
#include "fluids/FluidSurfaceRenderer.h"
#include "fluids/VolumeFluid.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <string_view>

using namespace eve::fluids;

namespace {
bool hasFluidZone(std::string_view name) {
    const auto& samples = eve::prof::Profiler::lastFrame();
    return std::any_of(samples.begin(), samples.end(), [&](const auto& sample) {
        return sample.module == "fluids" && sample.name == name && sample.count == 1 && sample.totalMs >= 0.0;
    });
}
}  // namespace

TEST_CASE("fluids.volume.profilerReportsSolverAndSurfaceWork") {
    eve::prof::Profiler::reset();
    eve::prof::Profiler::setEnabled(true);

    VolumeFluidSettings settings;
    settings.gravity = {0.f, 0.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = {0.f, 1.f, 0.f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());

    FluidSurfaceParams surface;
    surface.width  = 16;
    surface.height = 16;
    FluidSurfaceRenderer renderer(surface, false);
    renderer.renderVolume(*solver);

    eve::prof::Profiler::frameMark();
    CHECK(hasFluidZone("VolumeFluid::step"));
    CHECK(hasFluidZone("FluidSurfaceRenderer::renderVolume"));
    CHECK(hasFluidZone("FluidSurfaceRenderer::reconstruct"));

    eve::prof::Profiler::setEnabled(false);
    eve::prof::Profiler::reset();
}
