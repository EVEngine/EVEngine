#include <cmath>
#include <limits>
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/RenderTransformProjection.h"
#include "scene/SceneHost.h"
#include "scene/TransformSystem.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.authoring.bounds_use_vertices_and_clear_after_invalid_upload") {
    eve::graphics::Mesh mesh;
    REQUIRE(!mesh.localBounds().ok());
    mesh.boundsRadius = 10.f;
    REQUIRE(!mesh.localBounds().ok());
    const float vertices[]{-2.f, 4.f, 1.f, 3.f, -1.f, 7.f, 0.f, 2.f, -5.f};
    mesh.computeBounds(vertices, 3);
    auto bounds = mesh.localBounds();
    REQUIRE(bounds.ok());
    REQUIRE_EQ(bounds.value()[0], -2.f);
    REQUIRE_EQ(bounds.value()[1], -1.f);
    REQUIRE_EQ(bounds.value()[2], -5.f);
    REQUIRE_EQ(bounds.value()[3], 3.f);
    REQUIRE_EQ(bounds.value()[4], 4.f);
    REQUIRE_EQ(bounds.value()[5], 7.f);
    const float invalid[]{std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f};
    mesh.computeBounds(invalid, 1);
    REQUIRE(!mesh.localBounds().ok());
    mesh.computeBounds(vertices, 3);
    mesh.computeBounds(nullptr, 0);
    REQUIRE(!mesh.localBounds().ok());
}

TEST_CASE("graphics.authoring.scene_link_preserves_reflection_shear_and_direct_setter_mode") {
    using namespace eve::graphics;
    auto created = eve::scene::SceneHost::createHost("authoring-render-proof");
    REQUIRE(created.ok());
    auto* host = created.value();
    host->setTree(eve::scene::node("parent", {eve::scene::node("child")}));
    auto parent = host->findById("parent"), child = host->findById("child");
    REQUIRE(parent.ok());
    REQUIRE(child.ok());
    parent.value()->sx   = -2.f;
    parent.value()->sy   = 3.f;
    parent.value()->sz   = 0.5f;
    parent.value()->yaw  = 0.7f;
    child.value()->pitch = 0.4f;
    child.value()->yaw   = 0.5f;
    child.value()->x     = 2.f;
    auto* renderable     = Renderable3D::create();
    REQUIRE(host->linkRenderable3D("child", renderable));
    host->markSubtreeDirtyById("parent");
    eve::scene::TransformSystem::updateHost(host);
    const auto expected = child.value()->world;
    REQUIRE(renderable->transform()->hasLinkedWorld);
    const auto projected = detail::modelFromTransform(*renderable->transform());
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) REQUIRE(std::abs(projected[c][r] - expected[c][r]) < 1e-6f);
    REQUIRE(glm::determinant(projected) < 0.f);
    const float stretch = detail::sphereStretch(*renderable->transform());
    REQUIRE(stretch >= glm::length(glm::vec3(expected[0])));
    REQUIRE(stretch >= glm::length(glm::vec3(expected[1])));
    REQUIRE(stretch >= glm::length(glm::vec3(expected[2])));
    renderable->setPosition(8.f, 9.f, 10.f);
    REQUIRE(!renderable->transform()->hasLinkedWorld);
    REQUIRE_EQ(detail::modelFromTransform(*renderable->transform())[3][0], 8.f);
    host->markSubtreeDirtyById("child");
    eve::scene::TransformSystem::updateHost(host);
    REQUIRE(renderable->transform()->hasLinkedWorld);
    ecs::DestroyEntity(renderable);
    host->markSubtreeDirtyById("child");
    eve::scene::TransformSystem::updateHost(host);
    ecs::DestroyEntity(host);
}
