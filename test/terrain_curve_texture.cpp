#include "image/ImageData.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainCurveTexture.h"
#include "procgen/heightmap/TerrainStampScript.h"

#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include <zeroerr/unittest.h>

TEST_CASE("procgen.terrainCurveTexture.preservesPcgFirstPixelAndRangeContract") {
    eve::procgen::Heightmap curve(3, 1);
    curve.setHeight(0, 0, 0.25F);
    curve.setHeight(1, 0, 1.0F);
    curve.setHeight(2, 0, -0.5F);
    eve::image::ImageData output(4, 1, "RGBA32F");
    output.setPixel(0, 0, {1, 1, 1, 1});
    auto baked = eve::procgen::bakeTerrainCurveTexture(output, curve);
    REQUIRE(baked.ok());
    CHECK(baked.value().writtenPixels == 4);
    CHECK(baked.value().minimum == 0.25F);
    CHECK(baked.value().maximum == 1.0F);
    CHECK(output.getPixel(0, 0).r == 0.0F);
    CHECK(output.getPixel(1, 0).r == 0.625F);
    CHECK(output.getPixel(2, 0).r == 1.0F);
    CHECK(output.getPixel(3, 0).r == 0.25F);
    CHECK(output.getPixel(3, 0).a == 0.0F);
}

TEST_CASE("procgen.terrainCurveTexture.failureIsAtomic") {
    eve::procgen::Heightmap curve(2, 1);
    curve.setHeight(0, 0, 0);
    curve.setHeight(1, 0, std::numeric_limits<float>::quiet_NaN());
    eve::image::ImageData output(2, 1, "RGBA32F");
    output.setPixel(1, 0, {0.75F, 0, 0, 1});
    CHECK(!eve::procgen::bakeTerrainCurveTexture(output, curve).ok());
    CHECK(output.getPixel(1, 0).r == 0.75F);
    eve::procgen::Heightmap valid(2, 1);
    eve::image::ImageData twoRows(2, 2, "RGBA32F");
    CHECK(!eve::procgen::bakeTerrainCurveTexture(twoRows, valid).ok());
    CHECK(twoRows.getWidth() == 2);
    CHECK(twoRows.getHeight() == 2);
}

TEST_CASE("procgen.terrainCurveTexture.bindsReceiptToSquirrel") {
    eve::procgen::Heightmap curve(2, 1);
    curve.data() = {0.2F, 0.8F};
    eve::image::ImageData output(2, 1, "RGBA32F");
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    table.addClass<eve::image::ImageData>(
        "ImageData", std::function<eve::image::ImageData*()>([]() -> eve::image::ImageData* { return nullptr; }), true);
    eve::procgen::exposeHeightmap(table);
    eve::procgen::exposeTerrainCurveTexture(table);
    vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.run(vm.compileSource(R"(
        local result=eve.bakeTerrainCurveTexture(output(),curve());
        assert(result.ok && result.value.writtenPixels==2);
        assert(result.value.minimum>0.19 && result.value.minimum<0.21);
        assert(result.value.maximum==0.5);
    )"));
    CHECK(output.getPixel(0, 0).r == 0);
    CHECK(output.getPixel(1, 0).r == 0.5F);
}
