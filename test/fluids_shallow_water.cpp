#include "fluids/ShallowWaterField.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <vector>

using namespace eve::fluids;

TEST_CASE("fluids.shallowWater.createRejectsInvalidResolution") {
    ShallowWaterSettings settings;
    settings.resolutionX = 2;
    auto created         = ShallowWaterField::create(settings);
    REQUIRE(!created.ok());
}

TEST_CASE("fluids.shallowWater.fillAndQueriesSurfaceHeight") {
    ShallowWaterSettings settings;
    settings.resolutionX  = 32;
    settings.resolutionZ  = 32;
    settings.cellSize     = 0.5f;
    settings.initialDepth = 0.4f;
    settings.bedHeight    = 0.1f;
    settings.preferGpu    = false;
    auto created          = ShallowWaterField::create(settings);
    REQUIRE(created.ok());
    auto field = std::move(created).takeValue();
    REQUIRE(field->resolutionX() == 32);
    REQUIRE(!field->usingGpu());
    const float midX = settings.origin.x + 8.f;
    const float midZ = settings.origin.z + 8.f;
    REQUIRE(std::abs(field->surfaceHeightAt(midX, midZ) - 0.5f) < 0.02f);
    REQUIRE(std::abs(field->depthAt(midX, midZ) - 0.4f) < 0.02f);
}

TEST_CASE("fluids.shallowWater.impulsePropagatesAsWave") {
    ShallowWaterSettings settings;
    settings.resolutionX  = 48;
    settings.resolutionZ  = 48;
    settings.cellSize     = 0.25f;
    settings.initialDepth = 0.5f;
    settings.preferGpu    = false;
    auto created          = ShallowWaterField::create(settings);
    REQUIRE(created.ok());
    auto field = std::move(created).takeValue();

    const float cx = 6.f;
    const float cz = 6.f;
    REQUIRE(field->addImpulse(cx, cz, 1.4f, 0.45f).ok());
    const float probeX     = cx + 2.5f;
    const float beforeFar  = field->surfaceHeightAt(probeX, cz);
    for (int i = 0; i < 90; ++i) REQUIRE(field->step(1.f / 60.f).ok());
    const float afterFar = field->surfaceHeightAt(probeX, cz);
    // A disturbance near the center must move measurable energy outward.
    REQUIRE(std::abs(afterFar - beforeFar) > 0.001f);
}

TEST_CASE("fluids.shallowWater.capsuleLegDisplacesWater") {
    ShallowWaterSettings settings;
    settings.resolutionX  = 40;
    settings.resolutionZ  = 40;
    settings.cellSize     = 0.2f;
    settings.initialDepth = 0.55f;
    settings.preferGpu    = false;
    auto created          = ShallowWaterField::create(settings);
    REQUIRE(created.ok());
    auto field = std::move(created).takeValue();

    const float cx = 4.f;
    const float cz = 4.f;
    const float before = field->depthAt(cx, cz);

    ShallowWaterCollider leg;
    leg.label             = 1;
    leg.shape             = ShallowWaterColliderShape::Capsule;
    leg.center            = {cx, 0.35f, cz};
    leg.halfExtent        = {0.12f, 0.45f, 0.12f};
    leg.radius            = 0.12f;
    leg.velocity          = {0.8f, 0.f, 0.f};
    leg.displacementScale = 2.5f;
    leg.dragScale         = 1.8f;
    REQUIRE(field->setColliders(std::span(&leg, 1)).ok());

    for (int i = 0; i < 45; ++i) REQUIRE(field->step(1.f / 60.f).ok());

    const float underFoot = field->depthAt(cx, cz);
    const float beside    = field->depthAt(cx + 0.8f, cz);
    const auto  flow      = field->velocityAt(cx + 0.4f, cz);
    // Leg immersion lowers local depth and pushes water / momentum sideways.
    REQUIRE(underFoot < before);
    REQUIRE(beside > underFoot - 0.05f);
    REQUIRE(std::abs(flow.x) + std::abs(flow.y) > 0.01f);
}

TEST_CASE("fluids.shallowWater.setCollidersIsAtomicOnDuplicateLabels") {
    ShallowWaterSettings settings;
    settings.preferGpu = false;
    auto created       = ShallowWaterField::create(settings);
    REQUIRE(created.ok());
    auto field = std::move(created).takeValue();

    ShallowWaterCollider a;
    a.label  = 7;
    a.radius = 0.2f;
    REQUIRE(field->setColliders(std::span(&a, 1)).ok());
    REQUIRE(field->colliders().size() == 1);

    ShallowWaterCollider pair[2];
    pair[0]        = a;
    pair[1]        = a;
    pair[1].label  = 7;
    pair[1].center = {1.f, 1.f, 1.f};
    REQUIRE(!field->setColliders(std::span(pair, 2)).ok());
    REQUIRE(field->colliders().size() == 1);
    REQUIRE(field->colliders()[0].label == 7);
}

TEST_CASE("fluids.shallowWater.buildSurfaceMeshHasNormalsAndTriangles") {
    ShallowWaterSettings settings;
    settings.resolutionX = 16;
    settings.resolutionZ = 12;
    settings.preferGpu   = false;
    auto created         = ShallowWaterField::create(settings);
    REQUIRE(created.ok());
    auto field = std::move(created).takeValue();
    REQUIRE(field->addImpulse(2.f, 1.5f, 0.8f, 0.25f).ok());

    std::vector<float>    pos, nrm, uv;
    std::vector<uint32_t> idx;
    REQUIRE(field->buildSurfaceMesh(pos, nrm, uv, idx).ok());
    REQUIRE(pos.size() == size_t(16 * 12 * 3));
    REQUIRE(nrm.size() == pos.size());
    REQUIRE(uv.size() == size_t(16 * 12 * 2));
    REQUIRE(idx.size() == size_t(15 * 11 * 6));
    REQUIRE(idx.back() < uint32_t(16 * 12));
}

TEST_CASE("fluids.shallowWater.cpuMassStaysBoundedAfterSteps") {
    ShallowWaterSettings settings;
    settings.resolutionX  = 24;
    settings.resolutionZ  = 24;
    settings.initialDepth = 0.5f;
    settings.preferGpu    = false;
    auto created          = ShallowWaterField::create(settings);
    REQUIRE(created.ok());
    auto field = std::move(created).takeValue();
    REQUIRE(field->addImpulse(3.f, 3.f, 1.f, 0.2f).ok());

    double mass0 = 0.0;
    for (float h : field->depths()) mass0 += double(h);
    for (int i = 0; i < 60; ++i) REQUIRE(field->step(1.f / 60.f).ok());
    double mass1 = 0.0;
    for (float h : field->depths()) mass1 += double(h);
    // Lax-Friedrichs dissipates energy but should not explode mass.
    REQUIRE(mass1 > mass0 * 0.5);
    REQUIRE(mass1 < mass0 * 1.5);
}
