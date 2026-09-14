#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#include "Fixtures.h"

#include "graphics/Graphics.h"
#include "graphics/HairCards.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "window/Window.h"

using eve::graphics::Graphics;
using eve::graphics::Material;
using eve::graphics::Mesh;
using eve::graphics::Renderable3D;

TEST_CASE("graphics.HairCards.buildCardTopology") {
    auto data = eve::graphics::hair::buildCard(0.2f, 0.5f);
    CHECK_EQ(data.positions.size(), 12u);
    CHECK_EQ(data.normals.size(), 12u);
    CHECK_EQ(data.uvs.size(), 8u);
    CHECK_EQ(data.indices.size(), 6u);
}

TEST_CASE("graphics.HairCards.buildFromRootsAndPolyline") {
    eve::graphics::hair::CardRoot roots[2] = {};
    roots[0] = {0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.1f, 0.4f};
    roots[1] = {0.2f, 0.f, 0.f, 0.f, 1.f, 0.1f, 0.08f, 0.35f};
    auto fromRoots = eve::graphics::hair::buildCardsFromRoots(roots, 2);
    CHECK_EQ(fromRoots.indices.size(), 12u);

    const float poly[] = {0.f, 0.f, 0.f, 0.f, 0.2f, 0.f, 0.f, 0.45f, 0.05f};
    auto along = eve::graphics::hair::buildCardsAlongPolyline(poly, 3, 0.1f);
    CHECK_EQ(along.indices.size(), 12u);
}

TEST_CASE("graphics.HairCards.uploadMaterialAndLod") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 96, 64);
    REQUIRE(gfx != nullptr);

    Mesh *nearMesh = gfx->newHairCardMesh(0.15f, 0.4f);
    REQUIRE(nearMesh != nullptr);
    Mesh *farMesh = gfx->newHairCardMesh(0.25f, 0.35f);
    REQUIRE(farMesh != nullptr);

    Material *mat = gfx->newHairCardMaterial(nullptr);
    REQUIRE(mat != nullptr);
    CHECK(mat->getHair());
    CHECK(mat->getDoubleSided());
    CHECK(!mat->getDepthWrite());
    CHECK(!mat->getCastShadow());
    CHECK_EQ(mat->getSortPriority(), 10);

    auto *r = Renderable3D::create();
    REQUIRE(r != nullptr);
    eve::graphics::hair::configureCardLod(r, nearMesh, farMesh, 6.f);
    CHECK(r->getHair());
    CHECK_EQ(r->getMeshLodCount(), 2);

    delete mat;
    win->close();
}
