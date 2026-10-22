#include <chrono>
#include <filesystem>
#include <fstream>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <thread>
#include "fluids/VolumeFluid.h"
#include "graphics/AtmosphereVolume.h"
#include "graphics/FogVolume.h"
#include "graphics/RenderSystem3D.h"
#include "physics/World3D.h"
#include "physics/destruction/CollapseAnimation.h"
#include "physics/destruction/cook/AssemblyFractureSettings.h"
#include "physics/destruction/cook/VoronoiCook.h"
#include "physics/destruction/fx/CollapseDust.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::physics;
using namespace eve::fluids;
TEST_CASE("physics_collapse_dust.savedGasOutlivesSolverAndRejectsPartialLoad") {
    CollapseDust        clip;
    VolumeFluidSettings settings;
    settings.capacity = 32;
    settings.spacing  = 0.4f;
    settings.minimum  = {-8, -1, -8};
    settings.maximum  = {8, 12, 8};
    settings.gravity  = {0, -9.8f, 0};
    {
        auto                source = VolumeFluid::create(settings).expect("solver");
        VolumeFluidParticle p;
        p.position          = {0, 1, 0};
        p.material.phase    = VolumeFluidPhase::Gas;
        p.material.buoyancy = -0.2f;
        p.life              = 4.f;
        REQUIRE(source->emit(std::span(&p, 1)).ok());
        REQUIRE(clip.capture(*source, 0).ok());
        REQUIRE(!clip.capture(*source, 0).ok());
        for (int i = 0; i < 30; ++i) REQUIRE(source->step(1.f / 60.f, 2).ok());
        REQUIRE(clip.capture(*source, 0.5f).ok());
    }
    auto target = VolumeFluid::create(settings).expect("target");
    REQUIRE(clip.present(*target, 99).ok());
    const float raised = target->particleView()[0].position.y;
    REQUIRE(raised > 1.f);
    REQUIRE(clip.present(*target, -1).ok());
    REQUIRE(target->particleView()[0].position.y == 1.f);
    REQUIRE(!clip.present(*target, std::numeric_limits<float>::quiet_NaN()).ok());
    auto path = std::filesystem::temp_directory_path() / "eve-collapse-dust-test.evedust";
    std::filesystem::remove(path);
    REQUIRE(clip.save(path.string()).ok());
    REQUIRE(!clip.save(path.string()).ok());
    CollapseDust loaded;
    REQUIRE(loaded.load(path.string()).ok());
    REQUIRE(loaded.frameCount() == 2);
    REQUIRE(loaded.present(*target, 99).ok());
    REQUIRE(target->particleView()[0].position.y == raised);
    {
        std::ofstream out(path, std::ios::binary | std::ios::app);
        out.put('x');
    }
    REQUIRE(!loaded.load(path.string()).ok());
    REQUIRE(loaded.frameCount() == 2);
    REQUIRE(loaded.present(*target, 99).ok());
    REQUIRE(target->particleView()[0].position.y == raised);
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write("EVEDUST1", 8);
        out.put(9);
    }
    REQUIRE(!loaded.load(path.string()).ok());
    REQUIRE(loaded.frameCount() == 2);
    for (const auto& mutation : std::array<std::pair<int, unsigned>, 5>{
             {{8, 99u}, {12, 2049u}, {16, 0x7fc00000u}, {20, 0x7fc00000u}, {24, 1025u}}}) {
        std::filesystem::remove(path);
        REQUIRE(clip.save(path.string()).ok());
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(mutation.first);
        for (int byte = 0; byte < 4; ++byte) file.put(static_cast<char>((mutation.second >> (byte * 8)) & 255u));
        file.close();
        REQUIRE(!loaded.load(path.string()).ok());
        REQUIRE(loaded.frameCount() == 2);
    }
    std::filesystem::remove(path);
    REQUIRE(!loaded.prepareDepth(0, 430).ok());
    REQUIRE(!loaded.prepareDepth(5000, 430).ok());
    REQUIRE(loaded.prepareDepth(640, 430).ok());
}

TEST_CASE("physics_collapse_dust.savedGasInjectsExistingFogWithBoundedFootprints") {
    VolumeFluidSettings settings;
    settings.capacity          = 8;
    settings.spacing           = 0.2f;
    settings.minimum           = {-8, -2, -8};
    settings.maximum           = {8, 12, 8};
    auto                source = VolumeFluid::create(settings).expect("source");
    VolumeFluidParticle p;
    p.position       = {0, 1, 0};
    p.material.phase = VolumeFluidPhase::Gas;
    p.color          = {0.68f, 0.60f, 0.46f, 0.5f};
    p.life           = 4.f;
    REQUIRE(source->emit(std::span(&p, 1)).ok());
    CollapseDust clip;
    REQUIRE(clip.capture(*source, 0).ok());
    source.reset();
    eve::graphics::AtmosphereVolume actual, reference;
    actual.resize(64, 44, 64);
    reference.resize(64, 44, 64);
    actual.setDepthRange(0.2f, 65.f);
    reference.setDepthRange(0.2f, 65.f);
    auto projection = glm::perspectiveRH_ZO(glm::radians(39.f), 64.f / 44.f, 0.2f, 65.f);
    projection[1][1] *= -1.f;
    auto inverse = glm::inverse(projection * glm::lookAtRH(glm::vec3(0, 1, 8), glm::vec3(0, 1, 0), glm::vec3(0, 1, 0)));
    REQUIRE(clip.injectFog(actual, inverse, 0, 0.9f, 1.3f).ok());
    eve::graphics::FogVolume fog;
    fog.setShape("sphere");
    fog.setPosition(0, 1, 0);
    fog.setSize(2.6f, 2.6f, 2.6f);
    fog.setAlbedo(0.68f, 0.60f, 0.46f);
    fog.setExtinction(0.45f);
    fog.setEdgeFalloff(0.9f);
    fog.setNoise(0.72f, 1.4f, 42);
    reference.injectLocalVolumeFrustum(fog, inverse);
    float total = 0, maxDifference = 0;
    for (int z = 0; z < 64; ++z)
        for (int y = 0; y < 44; ++y)
            for (int x = 0; x < 64; ++x) {
                const float a = actual.at(x, y, z).extinction, b = reference.at(x, y, z).extinction;
                total += a;
                maxDifference = std::max(maxDifference, std::abs(a - b));
            }
    REQUIRE(total > 0);
    REQUIRE(maxDifference < 0.00001f);
    REQUIRE(!clip.injectFog(actual, inverse, 0, -1.f, 1.3f).ok());
    REQUIRE(!clip.injectFog(actual, glm::mat4(0.f), 0, 0.9f, 1.3f).ok());
}

TEST_CASE("physics_collapse_dust.ownedWorkerIsBoundedNonblockingAndOutlivesSources") {
    CollapseDust dust;
    {
        auto asset = GeometryCollectionAsset::makeWeldedBoxesFixture(1.f).expect("asset");
        destruction_cook::AssemblyFractureSettings settings;
        REQUIRE(settings.setGroupPolicy(0, {1, 20.f, 20.f}).ok());
        auto    cooked = destruction_cook::cookVoronoiAssembly(asset, settings).expect("cook");
        World3D world(0, -9.8f, 0, true);
        auto instance = GeometryCollectionInstance::createFractured(world, cooked.collection, cooked.geometry, 0, 0, 0)
                            .expect("instance");
        CollapseAnimation rigid;
        REQUIRE(rigid.capture(*instance, 0).ok());
        VolumeFluidSettings gas;
        gas.capacity               = 8;
        gas.minimum                = {-8, -2, -8};
        gas.maximum                = {8, 12, 8};
        auto                source = VolumeFluid::create(gas).expect("source");
        VolumeFluidParticle p;
        p.position       = {0, 1, 1};
        p.material.phase = VolumeFluidPhase::Gas;
        p.color          = {0.7f, 0.6f, 0.5f, 0.4f};
        REQUIRE(source->emit(std::span(&p, 1)).ok());
        REQUIRE(dust.capture(*source, 0).ok());
        REQUIRE(dust.prepareFog(rigid, 64, 64, 32, 24, 32).ok());
    }
    DustFogView view;
    view.view       = glm::lookAtRH(glm::vec3(0, 1, 8), glm::vec3(0, 1, 0), glm::vec3(0, 1, 0));
    auto projection = glm::perspectiveRH_ZO(glm::radians(view.fov), 1.f, view.nearDistance, view.farDistance);
    projection[1][1] *= -1.f;
    view.inverseViewProjection = glm::inverse(projection * view.view);
    REQUIRE(dust.requestFog(0, view, 0.9f, 1.3f).expect("submit") == DustFogRequest::Started);
    REQUIRE(dust.requestFog(1, view, 0.9f, 1.3f).expect("bounded") == DustFogRequest::Busy);
    REQUIRE(!dust.load("must-not-be-read.evedust").ok());
    std::optional<DustFogFrame> completed;
    for (int attempt = 0; attempt < 2000; ++attempt) {
        auto ready = dust.takeFogFrame().expect("nonblocking poll");
        if (ready) {
            completed = std::move(ready);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(completed.has_value());
    REQUIRE(completed->depth.size() == 64u * 64u * 4u);
    REQUIRE(completed->atlas.size() == 192u * 144u * 4u);
    unsigned opaque = 0;
    for (std::size_t i = 3; i < completed->atlas.size(); i += 4)
        if (completed->atlas[i] < 255) ++opaque;
    REQUIRE(opaque > 0);
    REQUIRE(dust.requestFog(0, view, 0.9f, 1.3f).expect("reuse") == DustFogRequest::Unchanged);
    auto invalid                  = view;
    invalid.inverseViewProjection = glm::mat4(0.f);
    REQUIRE(!dust.requestFog(1, invalid, 0.9f, 1.3f).ok());
    REQUIRE(dust.requestFog(1, view, 0.9f, 1.3f).expect("teardown job") == DustFogRequest::Started);
    // Destruction joins the sole CPU job; its captured sources are all owning values.
}
