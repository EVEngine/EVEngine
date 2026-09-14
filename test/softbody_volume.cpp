#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/Camera.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "asset/CanonicalMesh.h"
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/softbody/SoftBody3D.h"
#include "physics/softbody/SoftBodyCollision.h"
#include "physics/softbody/SoftBody3DDefinition.h"
#include "physics/softbody/SoftBodyModelDefinition.h"
#include "physics/softbody/cook/SoftBodyModelCooker.h"
#include "physics/softbody/editing/SoftBody3DSchema.h"
#include "physics/softbody/graphics/SoftBody3DRenderer.h"
#include "schema/SchemaRegistry.h"
#include "window/Window.h"

#include <SDL2/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>

using eve::physics::SoftBody3D;

namespace {

class PlaneCollisionProvider final : public eve::physics::softbody::ISoftBodyCollisionWorld {
public:
    [[nodiscard]] bool softBodyCollisionAvailable() const noexcept override { return available; }

    [[nodiscard]] eve::physics::softbody::SoftBodyContactState probeSoftBodyParticle(
        float, float y, float, float radius, eve::physics::softbody::SoftBodyContact& contact) const override {
        ++probeCount;
        contact = {};
        const float depth = planeY + radius - y;
        if (depth <= 0.f) return eve::physics::softbody::SoftBodyContactState::None;
        contact.hit         = true;
        contact.ny          = 1.f;
        contact.depth       = depth;
        contact.bodyId      = 17;
        contact.dynamicBody = true;
        return eve::physics::softbody::SoftBodyContactState::Hit;
    }

    void applySoftBodyImpulse(int bodyId, float, float y, float) override {
        if (bodyId == 17) {
            ++impulseCount;
            accumulatedImpulseY += y;
        }
    }

    bool              available           = true;
    float             planeY              = 0.f;
    mutable int       probeCount           = 0;
    int               impulseCount         = 0;
    float             accumulatedImpulseY = 0.f;
};

}  // namespace

TEST_CASE("softbody.volume.creationRejectsInvalidLattice") {
    auto invalid = SoftBody3D::create(1, 3, 3, 0.5f, 0.f, 0.f, 0.f);
    REQUIRE(!invalid);
    REQUIRE_EQ(invalid.status().code(), eve::StatusCode::Rejected);
}

TEST_CASE("softbody.volume.definitionRoundTripsAndCreatesRuntime") {
    eve::physics::SoftBody3DDefinition definition;
    definition.cols                  = 3;
    definition.rows                  = 4;
    definition.layers                = 5;
    definition.gravityY              = -4.5f;
    definition.deformationResistance = 0.67f;
    definition.iterations            = 7;
    definition.selfCollision         = true;
    auto       encoded               = definition.toValue();
    const bool encodedOk             = encoded.ok();
    REQUIRE(encodedOk);
    auto       decoded   = eve::physics::SoftBody3DDefinition::fromValue(encoded.value());
    const bool decodedOk = decoded.ok();
    REQUIRE(decodedOk);
    REQUIRE_EQ(decoded.value().cols, 3);
    REQUIRE_EQ(decoded.value().layers, 5);
    REQUIRE_EQ(decoded.value().iterations, 7);
    REQUIRE(decoded.value().selfCollision);
    auto       created   = SoftBody3D::create(decoded.value());
    const bool createdOk = created.ok();
    REQUIRE(createdOk);
    REQUIRE_EQ(created.value()->getParticleCount(), 60);
    REQUIRE_EQ(created.value()->getIterations(), 7);
    REQUIRE(created.value()->getSelfCollision());
    const auto* registered =
        eve::schema::SchemaRegistry::resolve(std::string(eve::physics::SoftBody3DDefinition::SchemaId),
                                             static_cast<int>(eve::physics::SoftBody3DDefinition::SchemaVersion));
    REQUIRE(registered != nullptr);
    auto       json   = encoded.value().toJson();
    const bool jsonOk = json.ok();
    REQUIRE(jsonOk);
    REQUIRE(eve::schema::SchemaRegistry::validate(registered->id, registered->version, json.value()).empty());
}

TEST_CASE("softbody.volume.definitionRejectsUnknownFieldsAndVersions") {
    eve::physics::SoftBody3DDefinition definition;
    auto                               encoded   = definition.toValue();
    const bool                         encodedOk = encoded.ok();
    REQUIRE(encodedOk);
    auto unknown                                          = encoded.value();
    (*unknown.getIf<eve::Value::Object>())["futureField"] = true;
    REQUIRE(!eve::physics::SoftBody3DDefinition::fromValue(unknown));
    auto future                                            = encoded.value();
    (*future.getIf<eve::Value::Object>())["schemaVersion"] = std::int64_t(2);
    REQUIRE(!eve::physics::SoftBody3DDefinition::fromValue(future));
}

TEST_CASE("softbody.volume.modelDefinitionRoundTripsMeshSamplingRecipe") {
    eve::physics::SoftBodyModelDefinition model;
    model.sourceMesh       = "asset://characters/slime.glb#mesh0";
    model.surfaceSampling  = eve::physics::SoftBodySurfaceSampling::Vertices;
    model.volumeSampling   = eve::physics::SoftBodyVolumeSampling::Voxels;
    model.volumeResolution = 24;
    model.shapeResolution  = 64;
    model.maxAnisotropy    = 2.5f;
    auto encoded = model.toValue();
    REQUIRE(encoded.ok());
    auto decoded = eve::physics::SoftBodyModelDefinition::fromValue(encoded.value());
    REQUIRE(decoded.ok());
    REQUIRE_EQ(decoded.value().sourceMesh, model.sourceMesh);
    REQUIRE_EQ(decoded.value().volumeResolution, 24);
    REQUIRE_EQ(decoded.value().shapeResolution, 64);
    REQUIRE(std::fabs(decoded.value().maxAnisotropy - 2.5f) < 0.0001f);
    auto registered = eve::physics::SoftBodyModelDefinition::ensureSchemaRegistered();
    REQUIRE(registered.ok());
    REQUIRE(eve::schema::SchemaRegistry::resolve("physics:softbody-model", 1) != nullptr);

    auto unknown = encoded.value();
    (*unknown.getIf<eve::Value::Object>())["futureField"] = true;
    REQUIRE(!eve::physics::SoftBodyModelDefinition::fromValue(unknown));
}

TEST_CASE("softbody.volume.cooksVertexSampledModelFromCanonicalMesh") {
    eve::asset::CanonicalMeshData mesh;
    mesh.positions = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};
    mesh.indices   = {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3};
    eve::physics::SoftBodyModelDefinition definition;
    definition.sourceMesh      = "asset://tetrahedron.evmesh";
    definition.surfaceSampling = eve::physics::SoftBodySurfaceSampling::Vertices;
    definition.volumeSampling  = eve::physics::SoftBodyVolumeSampling::None;
    auto cooked = eve::physics::softbody_cook::cookSoftBodyModel(mesh, definition);
    REQUIRE(cooked.ok());
    REQUIRE_EQ(cooked.value().particles.size(), std::size_t(4));
    REQUIRE_EQ(cooked.value().surfaceBindings.size(), std::size_t(4));
    REQUIRE_EQ(cooked.value().surfaceIndices.size(), std::size_t(12));
    REQUIRE_EQ(cooked.value().clusters.size(), std::size_t(4));
    REQUIRE(cooked.value().validate().ok());
}

TEST_CASE("softbody.volume.cookerRejectsUnsupportedVoxelRecipeTransactionally") {
    eve::asset::CanonicalMeshData mesh;
    mesh.positions = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
    mesh.indices   = {0, 1, 2};
    eve::physics::SoftBodyModelDefinition definition;
    definition.sourceMesh = "asset://triangle.evmesh";
    definition.surfaceSampling = eve::physics::SoftBodySurfaceSampling::Voxels;
    auto cooked = eve::physics::softbody_cook::cookSoftBodyModel(mesh, definition);
    REQUIRE(!cooked.ok());
    REQUIRE_EQ(cooked.status().code(), eve::StatusCode::Unsupported);
}

TEST_CASE("softbody.volume.creationSchemaMatchesDefinitionDefaults") {
    const auto schema = eve::physics_editing::softBody3DDefinitionSchema();
    REQUIRE_EQ(schema.typeId, std::string(eve::physics::SoftBody3DDefinition::SchemaId));
    REQUIRE_EQ(schema.version, eve::physics::SoftBody3DDefinition::SchemaVersion);
    REQUIRE_EQ(schema.properties.size(), std::size_t(20));
    auto iterations = schema.find(eve::editing::PropertyPath("iterations"));
    REQUIRE(iterations);
    REQUIRE_EQ(iterations->numeric.minimum.value(), 1.0);
    REQUIRE_EQ(iterations->numeric.maximum.value(), 32.0);
    REQUIRE_EQ(*iterations->defaultValue.getIf<std::int64_t>(), std::int64_t(5));
    auto selfCollision = schema.find(eve::editing::PropertyPath("selfCollision"));
    REQUIRE(selfCollision);
    REQUIRE_EQ(*selfCollision->defaultValue.getIf<bool>(), false);
}

TEST_CASE("softbody.volume.shapeMatchingRecoversAfterCompression") {
    auto       created   = SoftBody3D::create(4, 4, 4, 0.4f, -0.6f, 2.f, -0.6f);
    const bool createdOk = created.ok();
    REQUIRE(createdOk);
    auto body = std::move(created).takeValue();
    body->setGravity(0.f, 0.f, 0.f);
    body->setDeformationResistance(0.92f);
    body->setIterations(8);

    for (int z = 0; z < body->getLayers(); ++z) {
        for (int y = 0; y < body->getRows(); ++y) {
            for (int x = 0; x < body->getCols(); ++x) {
                const int index = (z * body->getRows() + y) * body->getCols() + x;
                body->setParticlePosition(index, body->getParticleX(index) * 0.55f, body->getParticleY(index),
                                          body->getParticleZ(index));
            }
        }
    }
    const float compressed = body->getVolumeRatio();
    REQUIRE_LT(compressed, 0.65f);
    for (int frame = 0; frame < 90; ++frame) body->update(1.f / 60.f);
    REQUIRE_GT(body->getVolumeRatio(), compressed + 0.2f);
    REQUIRE_GT(body->getVolumeRatio(), 0.85f);
}

TEST_CASE("softbody.volume.pinsAndWorldCollision") {
    auto       created   = SoftBody3D::create(3, 3, 3, 0.35f, -0.35f, 2.f, -0.35f);
    const bool createdOk = created.ok();
    REQUIRE(createdOk);
    auto                  body = std::move(created).takeValue();
    eve::physics::World3D world(0.f, -9.8f, 0.f, true);
    auto*                 ground = world.newBody("static", 0.f, -0.5f, 0.f);
    REQUIRE(ground != nullptr);
    ground->newBoxShape(6.f, 1.f, 6.f);
    body->setCollideWorld(&world);
    body->setParticleRadius(0.12f);
    body->setDeformationResistance(0.8f);
    body->pin(24);
    const float pinnedY = body->getParticleY(24);

    for (int frame = 0; frame < 180; ++frame) {
        world.update(1.f / 60.f);
        body->update(1.f / 60.f);
    }
    REQUIRE(std::fabs(body->getParticleY(24) - pinnedY) < 0.001f);
    for (int i = 0; i < body->getParticleCount(); ++i) {
        REQUIRE(std::isfinite(body->getParticleX(i)));
        REQUIRE(std::isfinite(body->getParticleY(i)));
        REQUIRE(std::isfinite(body->getParticleZ(i)));
        REQUIRE_GE(body->getParticleY(i), -0.2f);
    }
}

TEST_CASE("softbody.volume.coreAcceptsBackendNeutralCollisionProvider") {
    auto       created   = SoftBody3D::create(2, 2, 2, 0.25f, 0.f, 0.2f, 0.f);
    const bool createdOk = created.ok();
    REQUIRE(createdOk);
    auto body = std::move(created).takeValue();
    body->setParticleRadius(0.1f);

    PlaneCollisionProvider provider;
    provider.available = false;
    body->setCollisionWorld(&provider);
    body->update(1.f / 60.f);
    REQUIRE_EQ(provider.probeCount, 0);

    provider.available = true;
    for (int frame = 0; frame < 90; ++frame) body->update(1.f / 60.f);
    REQUIRE_GT(provider.probeCount, 0);
    REQUIRE_GT(provider.impulseCount, 0);
    REQUIRE_LT(provider.accumulatedImpulseY, 0.f);
    for (int i = 0; i < body->getParticleCount(); ++i) REQUIRE_GE(body->getParticleY(i), 0.099f);

    body->setCollisionWorld(nullptr);
}

TEST_CASE("softbody.volume.checkedStepTracksInjectedTime") {
    auto       created   = SoftBody3D::create(2, 2, 2, 0.5f, 0.f, 1.f, 0.f);
    const bool createdOk = created.ok();
    REQUIRE(createdOk);
    auto                             body = std::move(created).takeValue();
    eve::physics::SimulationSettings settings;
    settings.subStepCount = 3;
    auto stepped =
        body->step(eve::SimulationStep{eve::SimulationTick{1}, eve::Duration::fromNanoseconds(16666667)}, settings);
    const bool steppedOk = stepped.ok();
    REQUIRE(steppedOk);
    REQUIRE_EQ(body->observation().stepCount, 1u);
    REQUIRE(body->observation().lastTick == eve::SimulationTick{1});
}

TEST_CASE("softbody.volume.renderPreview") {
    auto* window   = eve::window::Window::create();
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(graphics != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 640;
    settings.height   = 420;
    settings.centered = true;
    REQUIRE(window->setWindowSettings(settings));
    graphics->setScreenReadbackEnabled(true);
    auto* camera = eve::graphics::Camera3D::createCamera();
    camera->setEye(2.7f, 2.4f, 4.f);
    camera->setTarget(0.f, 1.f, 0.f);
    camera->setAmbient(0.4f, 0.36f, 0.34f);
    camera->setActive(true);
    eve::graphics::RenderSystem3D::setDirectionalLight(-1.f, -1.f, -1.f, 1.4f, 1.2f, 1.f);

    auto       created   = SoftBody3D::create(4, 4, 4, 0.35f, -0.55f, 0.7f, -0.55f);
    const bool createdOk = created.ok();
    REQUIRE(createdOk);
    auto body = std::move(created).takeValue();
    body->setGravity(0.f, -3.f, 0.f);
    body->setDeformationResistance(0.82f);
    eve::physics::SoftBody3DRenderer renderer(body.get());
    renderer.setColor(0.96f, 0.34f, 0.18f, 1.f);
    graphics->setBackgroundColorRGBA(0.04f, 0.06f, 0.1f, 1.f);
    const glm::mat4 view =
        glm::lookAtRH(glm::vec3(2.7f, 2.4f, 4.f), glm::vec3(0.f, 1.f, 0.f), glm::vec3(0.f, 1.f, 0.f));
    glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(45.f), 640.f / 420.f, 0.1f, 100.f);
    projection[1][1] *= -1.f;
    for (int frame = 0; frame < 45; ++frame) {
        if (frame < 18) body->applyForce(0.08f, 0.f, 0.f);
        body->update(1.f / 60.f);
        graphics->begin3DFrame();
        graphics->setMesh3DViewProj(projection * view);
        graphics->setMesh3DView(view);
        graphics->setMesh3DClip(0.1f, 100.f);
        graphics->setMesh3DCameraPos(glm::vec3(2.7f, 2.4f, 4.f));
        renderer.draw(graphics);
        graphics->present();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
        }
        SDL_Delay(4);
    }
    const std::string output = std::string(EVENGINE_TEST_BINARY_DIR) + "/softbody_volume_preview.png";
    REQUIRE(graphics->saveFramePng(output));
    REQUIRE(std::filesystem::exists(output));
    REQUIRE(std::filesystem::file_size(output) > std::uintmax_t(1024));
    window->close();
}
