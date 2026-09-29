// Dedicated floor-texture recipe coverage lives in this TU so the large
// procgen.cpp suite stays process-isolated and focused.

#include "procgen/Params.h"
#include "procgen/texture/FloorTextures.h"
#include "procgen/texture/TextureRecipe.h"

#include "image/ImageData.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstring>
#include <memory>
#include <string>

using namespace eve::procgen;

TEST_CASE("procgen.floorTextures.wood.reproducibleLayouts") {
    TextureRecipeRegistry::instance().registerBuiltins();
    REQUIRE(TextureRecipeRegistry::instance().has("tex.floor.wood"));

    const char* layouts[] = {"planks", "staggered", "herringbone", "chevron", "parquet", "basket"};
    for (const char* layout : layouts) {
        Params p;
        p.setSeed(42);
        p.setSize(64, 64);
        p.setString("layout", layout);
        p.setString("tone", "oak");
        p.setInt("rows", 5);
        p.setInt("cols", 4);
        p.setFloat("gap", 0.05f);
        p.setInt("seamless", 1);

        std::string err;
        auto        a = TextureRecipeRegistry::instance().generate("tex.floor.wood", p, err);
        auto        b = TextureRecipeRegistry::instance().generate("tex.floor.wood", p, err);
        REQUIRE(static_cast<bool>(a));
        REQUIRE(static_cast<bool>(b));
        CHECK_EQ(a->getWidth(), 64);
        CHECK_EQ(a->getHeight(), 64);
        CHECK_EQ(a->getFormat(), std::string("RGBA8"));
        CHECK(std::memcmp(a->getData(), b->getData(), a->getSize()) == 0);
    }
}

TEST_CASE("procgen.floorTextures.tile.reproduciblePatterns") {
    TextureRecipeRegistry::instance().registerBuiltins();
    REQUIRE(TextureRecipeRegistry::instance().has("tex.floor.tile"));

    const char* patterns[] = {"square", "checker", "diamond", "hex", "subway",
                              "mosaic", "basket",  "herringbone"};
    for (const char* pattern : patterns) {
        Params p;
        p.setSeed(17);
        p.setSize(48, 48);
        p.setString("pattern", pattern);
        p.setString("palette", "ceramic");
        p.setInt("tilesX", 4);
        p.setInt("tilesY", 4);
        p.setFloat("grout", 0.07f);
        p.setInt("seamless", 1);

        std::string err;
        auto        a = TextureRecipeRegistry::instance().generate("tex.floor.tile", p, err);
        auto        b = TextureRecipeRegistry::instance().generate("tex.floor.tile", p, err);
        REQUIRE(static_cast<bool>(a));
        REQUIRE(static_cast<bool>(b));
        CHECK(std::memcmp(a->getData(), b->getData(), a->getSize()) == 0);
    }
}

TEST_CASE("procgen.floorTextures.paramsChangeOutput") {
    TextureRecipeRegistry::instance().registerBuiltins();

    Params oak;
    oak.setSeed(9);
    oak.setSize(32, 32);
    oak.setString("layout", "planks");
    oak.setString("tone", "oak");

    Params walnut = oak;
    walnut.setString("tone", "walnut");

    std::string err;
    auto        a = TextureRecipeRegistry::instance().generate("tex.floor.wood", oak, err);
    auto        b = TextureRecipeRegistry::instance().generate("tex.floor.wood", walnut, err);
    REQUIRE(static_cast<bool>(a));
    REQUIRE(static_cast<bool>(b));
    CHECK(std::memcmp(a->getData(), b->getData(), a->getSize()) != 0);

    Params square;
    square.setSeed(3);
    square.setSize(32, 32);
    square.setString("pattern", "square");
    square.setString("palette", "ceramic");

    Params checker = square;
    checker.setString("pattern", "checker");

    auto c = TextureRecipeRegistry::instance().generate("tex.floor.tile", square, err);
    auto d = TextureRecipeRegistry::instance().generate("tex.floor.tile", checker, err);
    REQUIRE(static_cast<bool>(c));
    REQUIRE(static_cast<bool>(d));
    CHECK(std::memcmp(c->getData(), d->getData(), c->getSize()) != 0);
}

TEST_CASE("procgen.floorTextures.resultApiAndDefaults") {
    Params p;
    p.setSeed(1);
    p.setSize(40, 40);
    auto wood = generateWoodFloorTexture(p);
    REQUIRE(wood.ok());
    REQUIRE(static_cast<bool>(wood.value()));
    CHECK_EQ(wood.value()->getWidth(), 40);

    auto tile = generateTileFloorTexture(p);
    REQUIRE(tile.ok());
    REQUIRE(static_cast<bool>(tile.value()));

    TextureRecipeRegistry::instance().registerBuiltins();
    Params filled;
    filled.setSize(16, 16);
    REQUIRE(TextureRecipeRegistry::instance().applyDefaults("tex.floor.wood", filled));
    CHECK_EQ(filled.getString("layout", ""), std::string("planks"));
    CHECK_EQ(filled.getString("tone", ""), std::string("oak"));
    REQUIRE(TextureRecipeRegistry::instance().applyDefaults("tex.floor.tile", filled));
    CHECK_EQ(filled.getString("pattern", ""), std::string("square"));
    CHECK_EQ(filled.getString("palette", ""), std::string("ceramic"));
}
